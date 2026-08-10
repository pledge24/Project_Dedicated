// ELO 순수 함수 시나리오 하네스. 의존성 0(node:assert) — DB 불필요.
// 실행: npm run match:elo  (전부 PASS여야 머지 — CLAUDE.md '알고리즘은 테스트 도구로 검증 후 머지')
import assert from 'node:assert/strict';

import { computeFfaEloDeltas } from '../src/match/result/elo.js';

const K = 32;

function sum(xs: number[]): number
{
    return xs.reduce((a, b) => a + b, 0);
}

const scenarios: Array<[string, () => void]> = [
    ['동일 1000점 4명, 등수 1/2/3/4 → 제로섬 + 단조감소 + 대칭', () =>
    {
        const d = computeFfaEloDeltas([1000, 1000, 1000, 1000], [1, 2, 3, 4], K);
        assert.equal(d.length, 4);
        assert.ok(Math.abs(sum(d)) <= 4, `제로섬 위반 sum=${sum(d)}`);
        assert.ok(d[0] > d[1] && d[1] > d[2] && d[2] > d[3], '등수 단조감소 아님');
        assert.ok(d[0] > 0 && d[3] < 0, '1위 +, 4위 - 아님');
        assert.equal(d[0], -d[3]); // 동일 점수면 대칭
        assert.equal(d[1], -d[2]);
    }],

    ['고수 1위 < 하수 1위 (업셋 보상)', () =>
    {
        const strong = computeFfaEloDeltas([1600, 1000, 1000, 1000], [1, 2, 3, 4], K);
        const weak   = computeFfaEloDeltas([1000, 1600, 1600, 1600], [1, 2, 3, 4], K);
        assert.ok(weak[0] > strong[0], `하수1위(${weak[0]}) > 고수1위(${strong[0]}) 여야`);
    }],

    ['전원 동점(동시 사망) → 변화 0', () =>
    {
        const d = computeFfaEloDeltas([1000, 1000, 1000, 1000], [1, 1, 1, 1], K);
        for (const x of d)
        {
            assert.equal(x, 0);
        }
    }],

    ['동률 1위 2명 → 둘 다 +, 나머지 -, 제로섬', () =>
    {
        const d = computeFfaEloDeltas([1000, 1000, 1000, 1000], [1, 1, 3, 4], K);
        assert.ok(d[0] > 0 && d[1] > 0, '동률 1위는 + 여야');
        assert.ok(d[2] < 0 && d[3] < 0, '하위는 - 여야');
        assert.ok(Math.abs(sum(d)) <= 4, `제로섬 위반 sum=${sum(d)}`);
    }],

    ['2명만 (n<4)도 동작 — 제로섬', () =>
    {
        const d = computeFfaEloDeltas([1200, 1000], [1, 2], K);
        assert.equal(d.length, 2);
        assert.ok(d[0] > 0 && d[1] < 0);
        assert.ok(Math.abs(sum(d)) <= 2);
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
