// 매칭 알고리즘 시나리오 하네스. 의존성 0(node:assert) + 시간 주입으로 결정론적.
// 실행: npm run match:sim  (전부 PASS여야 머지 — CLAUDE.md '알고리즘은 테스트 도구로 검증 후 머지')
import assert from 'node:assert/strict';

import { MatchQueue } from '../src/match/queue.js';
import type { MatchQueueParams } from '../src/match/queue.js';

// config 디폴트와 동일한 파라미터.
const PARAMS: MatchQueueParams = { playersPerMatch: 4, baseWindow: 200, expandRate: 50, maxWindow: 2000 };

function makeQueue(): MatchQueue<number>
{
    return new MatchQueue<number>(PARAMS);
}

// ref = userId(숫자)면 충분 — 알고리즘은 ref를 안 본다.
function add(q: MatchQueue<number>, userId: number, score: number, joinedAt: number): void
{
    q.enqueue({ userId, nickname: `u${userId}`, score, joinedAt, ref: userId });
}

function idsOf(group: { entries: { userId: number }[] }): number[]
{
    return group.entries.map((e) => e.userId).sort((a, b) => a - b);
}

const scenarios: Array<[string, () => void]> = [
    ['정확히 4명 동일 score → 즉시 1매치', () =>
    {
        const q = makeQueue();
        add(q, 1, 1000, 0); add(q, 2, 1000, 0); add(q, 3, 1000, 0); add(q, 4, 1000, 0);
        const m = q.runCycle(0);
        assert.equal(m.length, 1);
        assert.equal(m[0].entries.length, 4);
        assert.equal(q.size, 0);
    }],

    ['5명(근접 4 + 멀리 1, 멀리가 막 입장) → 근접 4명만 매치', () =>
    {
        const q = makeQueue();
        add(q, 1, 1000, 0); add(q, 2, 1010, 1); add(q, 3, 990, 2); add(q, 4, 1005, 3);
        add(q, 5, 3000, 4); // 윈도우 밖 + 최신(=최장대기 아님)
        const m = q.runCycle(10);
        assert.equal(m.length, 1);
        assert.deepEqual(idsOf(m[0]), [1, 2, 3, 4]);
        assert.equal(q.size, 1);
        assert.equal(q.snapshot()[0].userId, 5);
    }],

    ['윈도우 확장 — 처음엔 못 묶다가 대기 누적으로 묶임', () =>
    {
        const q = makeQueue();
        add(q, 1, 1000, 0); add(q, 2, 1500, 0); add(q, 3, 1000, 0); add(q, 4, 1500, 0);
        // seed=user1(1000), base=200. |1500-1000|=500 > 200 → 1500들 제외, 후보 2명.
        assert.equal(q.runCycle(0).length, 0);
        // window>=500 필요 → waitSec>=(500-200)/50=6s. t=5999ms는 499.95 → 아직 제외.
        assert.equal(q.runCycle(5999).length, 0);
        // t=6000ms → window=500 정확히 → |500|<=500 포함 → 매치.
        const m = q.runCycle(6000);
        assert.equal(m.length, 1);
        assert.equal(q.size, 0);
    }],

    ['선착순(FCFS) — 후보 5명 중 가장 오래 기다린 4명 선정', () =>
    {
        const q = makeQueue();
        add(q, 1, 1000, 0); add(q, 2, 1000, 10); add(q, 3, 1000, 20); add(q, 4, 1000, 30); add(q, 5, 1000, 40);
        const m = q.runCycle(100);
        assert.equal(m.length, 1);
        assert.deepEqual(idsOf(m[0]), [1, 2, 3, 4]);
        assert.equal(q.snapshot()[0].userId, 5); // 최신 1명 잔류
    }],

    ['MAX_WINDOW 캡 — 2001 차이는 영원히 제외, 2000 차이는 매치', () =>
    {
        const far = makeQueue();
        add(far, 1, 1000, 0); add(far, 2, 3001, 0); add(far, 3, 3001, 0); add(far, 4, 3001, 0);
        assert.equal(far.runCycle(1_000_000).length, 0); // |2001| > maxWindow 2000

        const edge = makeQueue();
        add(edge, 1, 1000, 0); add(edge, 2, 3000, 0); add(edge, 3, 3000, 0); add(edge, 4, 3000, 0);
        assert.equal(edge.runCycle(1_000_000).length, 1); // |2000| <= 2000
    }],

    ['중복 입장 거부 + 취소로 큐에서 제거', () =>
    {
        const q = makeQueue();
        add(q, 1, 1000, 0);
        assert.throws(() => add(q, 1, 1000, 0)); // 같은 userId 두 번
        assert.equal(q.size, 1);
        assert.equal(q.dequeue(1), true);
        assert.equal(q.size, 0);
        assert.equal(q.dequeue(999), false); // 없는 유저
    }],

    ['8명 동일 score → 한 tick에 2매치', () =>
    {
        const q = makeQueue();
        for (let i = 1; i <= 8; i++)
        {
            add(q, i, 1000, i);
        }
        const m = q.runCycle(1000);
        assert.equal(m.length, 2);
        assert.equal(q.size, 0);
    }],

    ['HOL 해소 — 고립 고MMR 시드 뒤의 4명이 막히지 않고 매치', () =>
    {
        const q = makeQueue();
        add(q, 99, 5000, 0); // 극단 MMR + 최장 대기(=시드 후보)
        add(q, 1, 1000, 1); add(q, 2, 1000, 2); add(q, 3, 1000, 3); add(q, 4, 1000, 4);
        const m = q.runCycle(1_000_000); // 큰 now라도 5000↔1000=4000 > maxWindow → 못 끌어옴
        assert.equal(m.length, 1);
        assert.deepEqual(idsOf(m[0]), [1, 2, 3, 4]);
        assert.equal(q.size, 1);
        assert.equal(q.snapshot()[0].userId, 99); // 아웃라이어만 잔류
    }],

    ['HOL 해소 — 고립 시드 뒤 두 클러스터가 한 사이클에 2매치', () =>
    {
        const q = makeQueue();
        add(q, 99, 5000, 0); // 최장 대기 아웃라이어
        for (let i = 1; i <= 4; i++)
        {
            add(q, i, 1000, i); // 저 클러스터
        }
        for (let i = 5; i <= 8; i++)
        {
            add(q, i, 2500, i); // 고 클러스터(5000과도 2500 > 2000)
        }
        const m = q.runCycle(1_000_000);
        assert.equal(m.length, 2);
        assert.equal(q.size, 1);
        assert.equal(q.snapshot()[0].userId, 99);
    }],

    ['앵커 강제 포함 — 성사된 방은 반드시 앵커(브리지)를 담는다', () =>
    {
        const q = makeQueue();
        // P들(joinedAt 0 = 최장대기)은 각자 앵커론 실패. X만 전원을 잇는 브리지 앵커.
        add(q, 10, 600, 0); add(q, 11, 600, 0); add(q, 12, 1400, 0); add(q, 13, 1400, 0);
        add(q, 1, 1000, 1000); // X: window=200+4*50=400 → [600,1400] 전원 포함(단, 짧게 대기)
        const m = q.runCycle(5000);
        assert.equal(m.length, 1);
        assert.ok(idsOf(m[0]).includes(1), '앵커 X(id 1)가 방에 포함돼야 함(단순 slice면 밀려남)');
    }],
];

let failed = 0;
for (const [name, fn] of scenarios)
{
    try
    {
        fn();
        console.log(`  ✓ ${name}`);
    }
    catch (err)
    {
        failed++;
        console.error(`  ✗ ${name}`);
        console.error('    ', err instanceof Error ? err.message : err);
    }
}

console.log(`\n${scenarios.length - failed}/${scenarios.length} passed`);
if (failed > 0)
{
    process.exit(1);
}
