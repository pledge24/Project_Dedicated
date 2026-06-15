// 매칭 도메인 로직 (service 레이어). 인메모리 큐 싱글톤을 소유한다.
// 프로세스 1개·포트 1개 가정(PROJECT_PLAN). 큐는 영속화하지 않는다.
import type { WebSocket } from 'ws';

import { config } from '../common/config.js';
import { AppError, Codes } from '../common/errors.js';
import type { MatchFoundData, MatchPlayer } from './protocol.js';
import { MatchQueue } from './queue.js';
import type { MatchGroup } from './queue.js';
import * as repo from './repository.js';

// ref = 그 유저의 WS 소켓. 매칭 성사 시 여기로 푸시한다.
const queue = new MatchQueue<WebSocket>({
    playersPerMatch: config.match.playersPerMatch,
    baseWindow: config.match.baseWindow,
    expandRate: config.match.expandRate,
    maxWindow: config.match.maxWindow,
});

/** 큐 입장. 현재 점수/닉네임을 DB에서 읽어 자리 생성. DB에 없으면 throw. */
export async function join(userId: number, ref: WebSocket): Promise<void>
{
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

/** 큐에서 제거(취소/연결 끊김 공용). 제거됐으면 true. */
export function leave(userId: number): boolean
{
    return queue.dequeue(userId);
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
    const players: MatchPlayer[] = group.entries.map((e, i) => ({
        userId: e.userId,
        nickname: e.nickname,
        score: e.score,
        slotIndex: i,
    }));

    const data: MatchFoundData = {
        matchId,
        server: { host: server.host, port: server.port },
        players,
        joinToken: '', // per-recipient — ws.handleMatch가 수신자별로 채움
    };

    return { data, targets: group.entries.map((e) => e.ref) };
}
