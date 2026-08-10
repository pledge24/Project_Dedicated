// 매칭 도메인 로직 (service 레이어). 인메모리 큐 싱글톤을 소유한다.
// 프로세스 1개·포트 1개 가정(PROJECT_PLAN). 큐는 영속화하지 않는다.
import { WebSocket } from 'ws';

import { config } from '../../common/config.js';
import { AppError, Codes } from '../../common/errors.js';
import * as pendingKicks from '../ds/pendingKicks.js';
import type { MatchFoundData } from '../protocol.types.js';
import * as resultRepo from '../result/result.repository.js';
import * as roster from '../roster/roster.service.js';
import { selectRequeue } from './formation.js';
import * as repo from './matchmaking.repository.js';
import { MatchQueue } from './queue.js';
import type { MatchGroup, QueueEntry } from './queue.js';

// ref = 그 유저의 WS 소켓. 매칭 성사 시 여기로 푸시한다.
const queue = new MatchQueue<WebSocket>({
    playersPerMatch: config.match.playersPerMatch,
    baseWindow: config.match.baseWindow,
    expandRate: config.match.expandRate,
    maxWindow: config.match.maxWindow,
});

// 매칭 확정(formation) 중인 userId 집합 — 끊김/재접속/취소가 전부 leave를 거치며 여기서 제거된다(단일 진실원).
const inFormation = new Set<number>();

/** 큐 입장. 중복·확정 중 재입장은 선 차단, 현재 점수/닉네임을 DB에서 읽어 자리 생성. DB에 없으면 throw. */
export async function join(userId: number, ref: WebSocket): Promise<void>
{
    // 큐 중복·확정 중 재입장 선 차단 — DB 조회 전에 거절
    if (queue.has(userId) || inFormation.has(userId))
    {
        throw new AppError(Codes.ALREADY_IN_QUEUE, '이미 매칭 진행 중입니다.');
    }

    const profile = await repo.findScoreAndNickname(userId);
    if (!profile)
    {
        throw new AppError(Codes.NOT_FOUND, '플레이어 정보를 찾을 수 없습니다.');
    }

    queue.enqueue({
        userId,
        nickname: profile.nickname,
        score: profile.score,
        joinedAt: Date.now(),
        ref,
    });
}

/** 큐에서 제거(취소/연결 끊김 공용). 제거됐으면 true. 확정 중이면 formation에서도 이탈시킨다. */
export function leave(userId: number): boolean
{
    const removed = queue.dequeue(userId);
    inFormation.delete(userId); // 확정 창 중 끊김/재접속/취소 → 재큐 대상에서 제외됨

    return removed;
}

/** 확정 시작 — 성사 그룹 전원을 formation에 등록. */
export function beginFormation(userIds: number[]): void
{
    for (const id of userIds)
    {
        inFormation.add(id);
    }
}

/** 확정 종료(성사 완료/할당 실패) — 재큐 없이 formation만 정리. */
export function endFormation(userIds: number[]): void
{
    for (const id of userIds)
    {
        inFormation.delete(id);
    }
}

/** 아직 이 확정에 유효한가. 끊김/재접속/취소 시 leave가 지운다. */
export function isInFormation(userId: number): boolean
{
    return inFormation.has(userId);
}

/**
 * 확정 중단 — 생존자(formation 유효 + 소켓 열림 + 큐 미존재)만 원 joinedAt/seq 보존해 재큐하고,
 * 그룹 전원을 formation에서 정리한다. 재큐된 인원 수 반환.
 */
export function abortFormation(entries: QueueEntry<WebSocket>[]): number
{
    const survivors = selectRequeue(entries, {
        isInFormation: (id) => inFormation.has(id),
        isOpen: (ref) => ref.readyState === WebSocket.OPEN,
        isQueued: (id) => queue.has(id),
    });
    for (const e of survivors)
    {
        queue.enqueue({ userId: e.userId, nickname: e.nickname, score: e.score, joinedAt: e.joinedAt, ref: e.ref, seq: e.seq });
    }
    for (const e of entries)
    {
        inFormation.delete(e.userId);
    }

    return survivors.length;
}

/** 세션 대체로 끊긴 유저가 게임중이면 kick 대기열에 표시 — DS가 폴링으로 회수한다. */
export function kickUserFromLiveMatch(userId: number): void
{
    const matchId = roster.findMatchByUser(userId);
    if (matchId)
    {
        pendingKicks.markKick(matchId, userId);
    }
}

/** runMatchmaking 결과 — 실 매치 그룹 + 봇전 대상(장기 대기자 1명씩). */
export interface MatchmakingResult
{
    groups: MatchGroup<WebSocket>[];
    botFills: QueueEntry<WebSocket>[];
}

/** 1초 사이클이 호출. 실 매치를 먼저 성사시키고, 남은 장기 대기자를 봇전 대상으로 넘긴다. */
export function runMatchmaking(now: number): MatchmakingResult
{
    const groups = queue.runCycle(now);
    const botFills = config.match.botFill.enabled
        ? queue.collectBotFillTimeouts(now, config.match.botFill.waitMs)
        : [];

    return { groups, botFills };
}

export function queueSize(): number
{
    return queue.size;
}

/**
 * 재입장 대상 매치 조회 — 클라가 match:found를 놓친(끊김·크래시·재실행) 경우의 유일한 복구 경로.
 *
 * "진행 중"은 roster 존재만으로 판정할 수 없다. roster는 결과 재제출 멱등을 위해 종료 후에도
 * sweep(DS 최대 수명)까지 남기 때문이다. 그래서 네 가지를 모두 통과해야 주소를 준다:
 *   1) roster에 이 유저가 있는 최신 매치가 있다      — findMatchByUser
 *   2) 그 매치가 아직 시작되지 않았다                 — 재입장 허용 창은 매치 시작 전까지(정책)
 *   3) 그 매치의 결과가 아직 저장되지 않았다          — 저장됐으면 경기가 끝난 것
 *   4) 이 유저가 아직 탈주로 정산되지 않았다          — DS가 KickedUserIds로 재입장을 거절할 대상
 * server 주소가 없는 roster는 재입장 주소를 만들 수 없으므로 대상에서 제외한다.
 */
export async function findRejoinableMatch(userId: number): Promise<MatchFoundData | null>
{
    const matchId = roster.findMatchByUser(userId);
    if (!matchId)
    {
        return null;
    }

    const found = roster.get(matchId);
    if (!found?.server)
    {
        return null;
    }

    // 시작한 매치는 DS가 PreLogin/InitNewPlayer에서 접속을 거절한다 — 주소를 계속 주면
    // 클라가 연결에 실패한 뒤에야 그 사실을 알게 된다. DB 조회 전에 메모리에서 끊는다.
    if (found.playStartedAt !== undefined)
    {
        return null;
    }

    const player = found.players.find((p) => p.userId === userId);
    if (!player || !player.joinToken)
    {
        return null;
    }

    if (await resultRepo.hasResult(matchId) || await resultRepo.isLeaverSettled(matchId, userId))
    {
        return null;
    }

    return { matchId, server: { host: found.server.host, port: found.server.port }, joinToken: player.joinToken };
}
