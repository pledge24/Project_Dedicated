// 재입장(GET /api/match/current) 통합 하네스(인프로세스).
// match-result-sim과 같은 방식으로 앱을 같은 프로세스에 띄운다 — roster 캐시가 인메모리라
// 별도 프로세스에선 시드할 수 없기 때문이다.
//
// 검증 대상은 "언제 주소를 주고 언제 주지 않는가" 한 가지다. roster 존재만으로는 진행 중을 알 수
// 없으므로(결과 재제출 멱등 때문에 종료 후에도 남는다) 결과·탈주 원장까지 봐야 한다는 것이 요지.
// 실행: npm run match:rejoin-sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import type { AddressInfo } from 'node:net';

import buildApp from '../src/app.js';
import { closePool } from '../src/common/db.js';
import * as jwtUtil from '../src/common/jwt.js';
import { getCurrentTokenVersion } from '../src/common/session.js';
import * as roster from '../src/match/roster.service.js';

const PASSWORD = 'rejointest123';
const MAP = 'default map';
const DS = { host: '127.0.0.1', port: 7777 };

interface HttpResult
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

    async function req(method: string, path: string, token?: string, body?: unknown): Promise<HttpResult>
    {
        const headers: Record<string, string> = { 'Content-Type': 'application/json' };
        if (token)
        {
            headers.Authorization = `Bearer ${token}`;
        }
        const res = await fetch(base + path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });

        return { status: res.status, body: (await res.json()) as HttpResult['body'] };
    }

    const get = (path: string, token?: string): Promise<HttpResult> => req('GET', path, token);
    const post = (path: string, body: unknown, token?: string): Promise<HttpResult> => req('POST', path, token, body);

    /**
     * 계정 1개 등록 → { userId, nickname, jwt }.
     * 토큰은 /login이 아니라 직접 서명한다 — 이 하네스는 계정을 여럿 만드는데
     * 로그인 rate limit(분당 5회)이 정상 동작하므로 6번째부터 429가 된다.
     * 검증 대상은 재입장 판정이지 로그인이 아니므로, 세션 규약(token_version 대조)만 지키면 충분하다.
     */
    async function seedUser(tag: string): Promise<{ userId: number; nickname: string; jwt: string }>
    {
        const suffix = randomBytes(3).toString('hex');
        const loginId = `rj${tag}${suffix}`;
        const nickname = `RJ${tag}${suffix}`;
        const reg = await post('/api/auth/register', { loginId, password: PASSWORD, nickname });
        assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);

        const userId = reg.body.data!.userId as number;
        const tokenVersion = await getCurrentTokenVersion(userId);
        assert.notEqual(tokenVersion, null, 'token_version 조회 실패');

        return { userId, nickname, jwt: jwtUtil.sign({ userId, nickname, tokenVersion: tokenVersion! }) };
    }

    /** 주소를 가진 roster 1건 등록(1인 매치 — 재입장 판정에 인원 수는 무관). */
    async function seedRoster(user: { userId: number; nickname: string }, opts: { withServer: boolean }): Promise<{ matchId: string; serverToken: string; joinToken: string }>
    {
        const matchId = `rejoin-${randomBytes(6).toString('hex')}`;
        const serverToken = randomBytes(24).toString('base64url');
        const joinToken = randomBytes(16).toString('base64url');
        await roster.register({
            matchId,
            serverToken,
            server: opts.withServer ? DS : undefined,
            mapName: MAP,
            startedAt: Date.now(),
            players: [{ userId: user.userId, nickname: user.nickname, joinToken }],
        });

        return { matchId, serverToken, joinToken };
    }

    const checks: Array<[string, () => Promise<void>]> = [
        ['인증 없음 → 401', async () =>
        {
            const r = await get('/api/match/current');
            assert.equal(r.status, 401, JSON.stringify(r.body));
        }],

        ['진행 중 매치 없음 → active:false', async () =>
        {
            const u = await seedUser('none');
            const r = await get('/api/match/current', u.jwt);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            assert.equal(r.body.data!.active, false);
        }],

        ['진행 중 매치 있음 → 주소·joinToken 반환', async () =>
        {
            const u = await seedUser('live');
            const m = await seedRoster(u, { withServer: true });
            const r = await get('/api/match/current', u.jwt);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            assert.equal(r.body.data!.active, true);
            assert.equal(r.body.data!.matchId, m.matchId);
            assert.equal(r.body.data!.joinToken, m.joinToken);
            assert.deepEqual(r.body.data!.server, DS);
        }],

        ['결과가 저장된 매치 → active:false (roster는 남아있어도)', async () =>
        {
            const u = await seedUser('done');
            const m = await seedRoster(u, { withServer: true });

            const before = await get('/api/match/current', u.jwt);
            assert.equal(before.body.data!.active, true, '사전 조건: 결과 저장 전에는 재입장 대상');

            const res = await post('/api/match/result', {
                matchId: m.matchId,
                mapName: MAP,
                durationSec: 42,
                endReason: 'winner',
                results: [{ userId: u.userId, slotIndex: 0, placement: 1, livesLeft: 2 }],
            }, m.serverToken);
            assert.equal(res.status, 200, JSON.stringify(res.body));

            // roster는 재제출 멱등을 위해 남아 있다 — 그래도 재입장 대상이 아니어야 한다.
            assert.ok(roster.get(m.matchId), 'roster가 결과 저장으로 지워지면 이 검사의 의미가 없다');

            const after = await get('/api/match/current', u.jwt);
            assert.equal(after.body.data!.active, false, '끝난 매치로 되돌려 보내면 안 된다');
        }],

        ['탈주 정산된 유저 → active:false (DS가 재입장을 거절할 대상)', async () =>
        {
            const u = await seedUser('left');
            const m = await seedRoster(u, { withServer: true });

            const settle = await post(`/api/match/${m.matchId}/leaver`, { userId: u.userId }, m.serverToken);
            assert.equal(settle.status, 200, JSON.stringify(settle.body));

            const r = await get('/api/match/current', u.jwt);
            assert.equal(r.body.data!.active, false);
        }],

        ['주소 없는 구버전 roster → active:false', async () =>
        {
            const u = await seedUser('addr');
            await seedRoster(u, { withServer: false });
            const r = await get('/api/match/current', u.jwt);
            assert.equal(r.body.data!.active, false, '주소를 모르면 보낼 곳이 없다');
        }],

        ['남의 매치는 안 보인다', async () =>
        {
            const owner = await seedUser('own');
            const other = await seedUser('oth');
            await seedRoster(owner, { withServer: true });
            const r = await get('/api/match/current', other.jwt);
            assert.equal(r.body.data!.active, false);
        }],

        ['매치를 두 번 한 유저 → 최신 매치를 준다', async () =>
        {
            const u = await seedUser('two');
            await seedRoster(u, { withServer: true });
            await new Promise((resolve) => setTimeout(resolve, 5)); // startedAt 역전 방지
            const second = await seedRoster(u, { withServer: true });

            const r = await get('/api/match/current', u.jwt);
            assert.equal(r.body.data!.active, true);
            assert.equal(r.body.data!.matchId, second.matchId, '오래된 매치를 주면 죽은 DS로 보낸다');
        }],
    ];

    let passed = 0;
    for (const [name, fn] of checks)
    {
        await fn();
        passed += 1;
        console.log(`  ✓ ${name}`);
    }

    console.log(`\n${passed}/${checks.length} passed — 끊긴 클라가 진행 중 매치로만 되돌아간다`);

    server.close();
    await closePool();
}

main().catch((err) =>
{
    console.error(err);
    process.exit(1);
});
