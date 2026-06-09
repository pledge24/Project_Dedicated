// 매치 결과 엔드포인트 통합 하네스(인프로세스). 앱을 같은 프로세스에 띄워 roster를 직접 시드한다
// (roster는 인메모리라 별도 프로세스에선 못 건드림). 언리얼 DS 없이 F5a를 끝까지 검증.
// 실행: npm run match:result-sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import type { AddressInfo } from 'node:net';

import buildApp from '../src/app.js';
import { closePool } from '../src/common/db.js';
import * as roster from '../src/match/roster.js';

const PASSWORD = 'resulttest123';
const MAP = '/Game/D1/Maps/MP_Ingame';

interface PostResult
{
    status: number;
    body: { ok: boolean; data?: Record<string, unknown>; error?: { code: string; message: string } };
}

async function main(): Promise<void>
{
    const app = buildApp();
    const server = app.listen(0);
    await new Promise<void>((resolve) => server.once('listening', () => resolve()));
    const { port } = server.address() as AddressInfo;
    const base = `http://127.0.0.1:${port}`;

    async function post(path: string, body: unknown, token?: string): Promise<PostResult>
    {
        const headers: Record<string, string> = { 'Content-Type': 'application/json' };
        if (token)
        {
            headers.Authorization = `Bearer ${token}`;
        }
        const res = await fetch(base + path, { method: 'POST', headers, body: JSON.stringify(body) });

        return { status: res.status, body: (await res.json()) as PostResult['body'] };
    }

    // 1. 고유 4계정 등록 → userId 확보
    const suffix = randomBytes(3).toString('hex'); // 영소문자+숫자 6자
    const users: Array<{ userId: number; nickname: string }> = [];
    for (let i = 0; i < 4; i++)
    {
        const loginId = `rt${suffix}${i}`;
        const nickname = `RT${suffix}${i}`;
        const reg = await post('/api/auth/register', { loginId, password: PASSWORD, nickname });
        assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);
        users.push({ userId: reg.body.data!.userId as number, nickname });
    }

    // 2. roster 시드(같은 프로세스라 서버가 보는 그 Map) + 결과 본문
    const matchId = `sim-${suffix}-${randomBytes(4).toString('hex')}`;
    const serverToken = randomBytes(24).toString('base64url');
    roster.register({
        matchId,
        serverToken,
        mapName: MAP,
        startedAt: Date.now(),
        players: users.map((u, i) => ({ userId: u.userId, slotIndex: i, nickname: u.nickname })),
    });

    const resultBody = {
        matchId,
        mapName: MAP,
        durationSec: 123,
        endReason: 'winner',
        results: users.map((u, i) => ({ userId: u.userId, placement: i + 1, livesLeft: i === 0 ? 2 : 0 })),
    };

    const checks: Array<[string, () => Promise<void>]> = [
        ['Authorization 없음 → 401', async () =>
        {
            const r = await post('/api/match/result', resultBody);
            assert.equal(r.status, 401, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'SERVER_AUTH_REQUIRED');
        }],

        ['잘못된 서버 토큰 → 403', async () =>
        {
            const r = await post('/api/match/result', resultBody, 'wrong-token');
            assert.equal(r.status, 403, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'INVALID_SERVER_TOKEN');
        }],

        ['없는 matchId → 404', async () =>
        {
            const r = await post('/api/match/result', { ...resultBody, matchId: 'no-such-match' }, serverToken);
            assert.equal(r.status, 404, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'MATCH_NOT_FOUND');
        }],

        ['외부인 포함 → 400', async () =>
        {
            const bad = { ...resultBody, results: resultBody.results.map((e, i) => i === 3 ? { ...e, userId: 999_999_999 } : e) };
            const r = await post('/api/match/result', bad, serverToken);
            assert.equal(r.status, 400, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'INVALID_RESULT');
        }],

        ['정상 제출 → 200 + 제로섬 + 1위>0>4위', async () =>
        {
            const r = await post('/api/match/result', resultBody, serverToken);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const ps = r.body.data!.participants as Array<{ placement: number; scoreDelta: number; scoreAfter: number }>;
            assert.equal(ps.length, 4);
            const sum = ps.reduce((a, p) => a + p.scoreDelta, 0);
            assert.ok(Math.abs(sum) <= 4, `제로섬 위반 sum=${sum}`);
            const first = ps.find((p) => p.placement === 1)!;
            const last = ps.find((p) => p.placement === 4)!;
            assert.ok(first.scoreDelta > 0, `1위 scoreDelta=${first.scoreDelta} 양수 아님`);
            assert.ok(last.scoreDelta < 0, `4위 scoreDelta=${last.scoreDelta} 음수 아님`);
            assert.equal(first.scoreAfter, 1000 + first.scoreDelta);
        }],

        ['중복 제출 → 409', async () =>
        {
            const r = await post('/api/match/result', resultBody, serverToken);
            assert.equal(r.status, 409, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'RESULT_ALREADY_SUBMITTED');
        }],
    ];

    let failed = 0;
    for (const [name, fn] of checks)
    {
        try
        {
            await fn();
            console.log(`  ✓ ${name}`);
        }
        catch (err)
        {
            failed++;
            console.error(`  ✗ ${name}`);
            console.error('    ', err instanceof Error ? err.message : err);
        }
    }

    console.log(`\n${checks.length - failed}/${checks.length} passed`);

    server.close();
    await closePool();

    if (failed > 0)
    {
        process.exit(1);
    }
}

void main();
