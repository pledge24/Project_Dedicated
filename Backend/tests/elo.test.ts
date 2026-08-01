import { describe, expect, it } from 'vitest';

import { computeFfaEloDeltas } from '../src/match/elo.js';

const K = 32;

describe('computeFfaEloDeltas', () =>
{
    it('동일 점수 4인 — 1위가 최대 획득, 4위가 최대 손실, 합은 0 근처', () =>
    {
        const deltas = computeFfaEloDeltas([1000, 1000, 1000, 1000], [1, 2, 3, 4], K);

        expect(deltas[0]).toBeGreaterThan(0);
        expect(deltas[3]).toBeLessThan(0);
        expect(deltas[0]).toBeGreaterThan(deltas[1]);
        expect(deltas[2]).toBeGreaterThan(deltas[3]);
        // 반올림 오차 허용(±N).
        expect(Math.abs(deltas.reduce((a, b) => a + b, 0))).toBeLessThanOrEqual(4);
    });

    it('고점수 플레이어가 지면 동일 점수끼리보다 더 크게 잃는다', () =>
    {
        const evenLoss = computeFfaEloDeltas([1000, 1000], [2, 1], K)[0];
        const favoredLoss = computeFfaEloDeltas([1400, 1000], [2, 1], K)[0];

        expect(favoredLoss).toBeLessThan(evenLoss);
    });

    it('전원 동순위면 동일 점수에서 변화 0', () =>
    {
        expect(computeFfaEloDeltas([1200, 1200, 1200], [1, 1, 1], K)).toEqual([0, 0, 0]);
    });

    it('1인 이하는 변화 없음', () =>
    {
        expect(computeFfaEloDeltas([1000], [1], K)).toEqual([0]);
        expect(computeFfaEloDeltas([], [], K)).toEqual([]);
    });

    it('ratings/placements 길이 불일치는 throw', () =>
    {
        expect(() => computeFfaEloDeltas([1000, 1000], [1], K)).toThrow();
    });
});
