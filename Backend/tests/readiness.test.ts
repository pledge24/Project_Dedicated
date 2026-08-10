import { describe, expect, it } from 'vitest';

import { fail, signal, waitForReady } from '../src/match/ds/readiness.js';

describe('readiness gate', () =>
{
    it('signal이 오면 resolve', async () =>
    {
        const gate = waitForReady('m1', 1000);
        signal('m1');

        await expect(gate).resolves.toBeUndefined();
    });

    it('fail이 오면 즉시 reject(타임아웃 대기 없음)', async () =>
    {
        const gate = waitForReady('m2', 10_000);
        fail('m2', 'DS 조기 사망');

        await expect(gate).rejects.toThrow('DS 조기 사망');
    });

    it('타임아웃까지 신호가 없으면 reject', async () =>
    {
        await expect(waitForReady('m3', 30)).rejects.toThrow('타임아웃');
    });

    it('gate 없는 matchId의 signal/fail은 no-op', () =>
    {
        expect(() => signal('none')).not.toThrow();
        expect(() => fail('none', 'x')).not.toThrow();
    });

    it('소비된 gate에 재신호해도 무해(멱등)', async () =>
    {
        const gate = waitForReady('m4', 1000);
        signal('m4');
        signal('m4');
        fail('m4', '늦은 실패');

        await expect(gate).resolves.toBeUndefined();
    });
});
