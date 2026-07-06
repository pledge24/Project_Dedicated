// 매칭 도메인 로직 (service 레이어). 인메모리 큐 싱글톤을 소유한다.
// 프로세스 1개·포트 1개 가정(PROJECT_PLAN). 큐는 영속화하지 않는다.
import { WebSocket } from 'ws';

import { config } from '../common/config.js';
import { AppError, Codes } from '../common/errors.js';
import { selectRequeue } from './formation.js';
import * as repo from './matchmaking.repository.js';
import type { MatchFoundData } from './protocol.js';
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

/** 1초 사이클이 호출. 성사된 매치 그룹들을 반환. */
export function runMatching(now: number): MatchGroup<WebSocket>[]
{
    return queue.runCycle(now);
}

export function queueSize(): number
{
    return queue.size;
}

/** 매치 그룹 + 할당된 서버 주소 → match:found payload + 푸시 대상 소켓. */
export function buildMatchFound(
    group: MatchGroup<WebSocket>,
    matchId: string,
    server: { host: string; port: number }
): { data: MatchFoundData; targets: WebSocket[] }
{
    const data: MatchFoundData = {
        matchId,
        server: { host: server.host, port: server.port },
        joinToken: '', // per-recipient — ws.handleMatch가 수신자별로 채움
    };

    return { data, targets: group.entries.map((e) => e.ref) };
}
