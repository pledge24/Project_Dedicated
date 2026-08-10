import { describe, expect, it } from 'vitest';

import { MatchQueue } from '../src/match/matchmaking/queue.js';

const PARAMS = { playersPerMatch: 4, baseWindow: 200, expandRate: 50, maxWindow: 2000 };

function makeQueue(): MatchQueue<string>
{
    return new MatchQueue<string>(PARAMS);
}

function enqueue(queue: MatchQueue<string>, userId: number, score: number, joinedAt: number): void
{
    queue.enqueue({ userId, nickname: `u${userId}`, score, joinedAt, ref: `ref${userId}` });
}

describe('MatchQueue', () =>
{
    it('동일 점수 4인이 모이면 한 사이클에 그룹 성사', () =>
    {
        const queue = makeQueue();
        for (let i = 1; i <= 4; i++)
        {
            enqueue(queue, i, 1000, 0);
        }

        const groups = queue.runCycle(0);

        expect(groups).toHaveLength(1);
        expect(groups[0].entries).toHaveLength(4);
        expect(queue.size).toBe(0);
    });

    it('정원 미달이면 그룹 없음 + 큐 유지', () =>
    {
        const queue = makeQueue();
        for (let i = 1; i <= 3; i++)
        {
            enqueue(queue, i, 1000, 0);
        }

        expect(queue.runCycle(0)).toHaveLength(0);
        expect(queue.size).toBe(3);
    });

    it('maxWindow를 넘는 점수차는 아무리 기다려도 미매칭', () =>
    {
        const queue = makeQueue();
        enqueue(queue, 1, 1000, 0);
        enqueue(queue, 2, 1000, 0);
        enqueue(queue, 3, 1000, 0);
        enqueue(queue, 4, 1000 + PARAMS.maxWindow + 1, 0);

        // 오랜 대기로 윈도우가 상한까지 확장돼도 격차가 상한+1이라 미매칭.
        expect(queue.runCycle(10 * 60 * 1000)).toHaveLength(0);
        expect(queue.size).toBe(4);
    });

    it('dequeue — 있으면 제거·true, 없으면 false', () =>
    {
        const queue = makeQueue();
        enqueue(queue, 1, 1000, 0);

        expect(queue.dequeue(1)).toBe(true);
        expect(queue.size).toBe(0);
        expect(queue.dequeue(1)).toBe(false);
    });

    it('collectBotFillTimeouts — 임계 넘은 엔트리만 큐에서 빠져 반환', () =>
    {
        const queue = makeQueue();
        enqueue(queue, 1, 1000, 0);       // 오래 대기
        enqueue(queue, 2, 5000, 9_000);   // 최근 입장

        const timedOut = queue.collectBotFillTimeouts(30_000, 30_000);

        expect(timedOut.map((e) => e.userId)).toEqual([1]);
        expect(queue.size).toBe(1);
        expect(queue.has(2)).toBe(true);
    });
});
