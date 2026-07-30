// 단일 세션 강제(중복 로그인 방지) 통합 하네스(인프로세스). 앱+매칭 WS를 같은 프로세스에
// 띄우고, 같은 계정으로 두 번 로그인한 뒤 옛 토큰이 HTTP·WS 양쪽에서 거절되는지 검증한다.
// 실행: npm run auth:sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import http from 'node:http';
import type { AddressInfo } from 'node:net';
import { WebSocket } from 'ws';

import buildApp from '../src/app.js';
import { closePool } from '../src/common/db.js';
import * as roster from '../src/match/roster.js';
import { attachMatchWebSocket } from '../src/match/ws.js';

const PASSWORD = 'sesstest123';

interface ApiResult
{
    status: number;
    body: { ok: boolean; data?: Record<string, unknown>; error?: { code: string; message: string } };
}

/** WS 업그레이드 결과: open=연결 성공, 아니면 거절 status(예: 401). 최초 이벤트만 채택. */
function probeWs(wsUrl: string, token: string): Promise<{ open: boolean; status?: number }>
{
    return new Promise((resolve, reject) =>
    {
        let settled = false;
        let timer: ReturnType<typeof setTimeout>;

        const done = (fn: () => void): void =>
        {
            if (settled)
            {
                return;
            }
            settled = true;
            clearTimeout(timer);
            fn();
        };

        const ws = new WebSocket(wsUrl, { headers: { Authorization: `Bearer ${token}` } });
        timer = setTimeout(() => done(() =>
        {
            ws.terminate();
            reject(new Error('WS 응답 타임아웃'));
        }), 5000);

        ws.on('open', () => done(() =>
        {
            ws.close();
            resolve({ open: true });
        }));

        ws.on('unexpected-response', (_req, res) => done(() =>
        {
            ws.terminate();
            resolve({ open: false, status: res.statusCode });
        }));

        ws.on('error', (err) => done(() =>
        {
            reject(err);
        }));
    });
}

/** WS를 열어 open까지 대기 후 소켓 반환. 거절/에러 시 reject. */
function openWs(wsUrl: string, token: string): Promise<WebSocket>
{
    return new Promise((resolve, reject) =>
    {
        const ws = new WebSocket(wsUrl, { headers: { Authorization: `Bearer ${token}` } });
        const timer = setTimeout(() =>
        {
            ws.terminate();
            reject(new Error('WS open 타임아웃'));
        }, 5000);

        ws.on('open', () =>
        {
            clearTimeout(timer);
            resolve(ws);
        });

        ws.on('unexpected-response', (_req, res) =>
        {
            clearTimeout(timer);
            ws.terminate();
            reject(new Error(`WS 거절 status=${res.statusCode}`));
        });

        // 서버측 terminate가 내는 에러가 unhandled 'error'로 죽지 않게 리스너를 유지한다.
        ws.on('error', (err) => reject(err));
    });
}

/** 다음 서버 메시지 1건을 파싱해 반환. */
function nextMessage(ws: WebSocket): Promise<{ type: string; ok: boolean }>
{
    return withTimeout(new Promise((resolve) =>
    {
        ws.once('message', (raw) => resolve(JSON.parse(raw.toString()) as { type: string; ok: boolean }));
    }), 5000, 'WS 메시지 타임아웃');
}

/** promise가 ms 안에 완료되지 않으면 msg로 실패. */
function withTimeout<T>(promise: Promise<T>, ms: number, msg: string): Promise<T>
{
    return new Promise<T>((resolve, reject) =>
    {
        const timer = setTimeout(() => reject(new Error(msg)), ms);
        promise.then(
            (value) =>
            {
                clearTimeout(timer);
                resolve(value);
            },
            (err) =>
            {
                clearTimeout(timer);
                reject(err);
            }
        );
    });
}

async function main(): Promise<void>
{
    const app = buildApp();
    const server = http.createServer(app);
    const matchWs = attachMatchWebSocket(server);
    server.listen(0);
    await new Promise<void>((resolve) => server.once('listening', () => resolve()));
    const { port } = server.address() as AddressInfo;
    const base = `http://127.0.0.1:${port}`;
    const wsUrl = `ws://127.0.0.1:${port}/ws/match`;

    async function post(path: string, body: unknown): Promise<ApiResult>
    {
        const res = await fetch(base + path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });

        return { status: res.status, body: (await res.json()) as ApiResult['body'] };
    }

    async function getMe(token?: string): Promise<ApiResult>
    {
        return get('/api/auth/me', token);
    }

    async function get(path: string, token?: string): Promise<ApiResult>
    {
        const headers: Record<string, string> = {};
        if (token)
        {
            headers.Authorization = `Bearer ${token}`;
        }
        const res = await fetch(base + path, { headers });

        return { status: res.status, body: (await res.json()) as ApiResult['body'] };
    }

    // 1. 신규 계정 등록
    const suffix = randomBytes(3).toString('hex');
    const loginId = `sess${suffix}`;
    const nickname = `Sess${suffix}`;
    const reg = await post('/api/auth/register', { loginId, password: PASSWORD, nickname });
    assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);
    const userId = reg.body.data!.userId as number;

    // 2. 같은 계정으로 2회 로그인 → 옛(token1)/새(token2) 토큰 확보
    const login1 = await post('/api/auth/login', { loginId, password: PASSWORD });
    assert.equal(login1.body.ok, true, `1차 login 실패: ${JSON.stringify(login1.body)}`);
    const token1 = login1.body.data!.token as string;

    const login2 = await post('/api/auth/login', { loginId, password: PASSWORD });
    assert.equal(login2.body.ok, true, `2차 login 실패: ${JSON.stringify(login2.body)}`);
    const token2 = login2.body.data!.token as string;

    const checks: Array<[string, () => Promise<void>]> = [
        ['재로그인은 새 토큰을 발급', async () =>
        {
            assert.notEqual(token1, token2, '2차 로그인이 동일 토큰 반환');
        }],

        ['옛 토큰 /me → 401 SESSION_SUPERSEDED', async () =>
        {
            const r = await getMe(token1);
            assert.equal(r.status, 401, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'SESSION_SUPERSEDED');
        }],

        ['새 토큰 /me → 200 + 본인', async () =>
        {
            const r = await getMe(token2);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            assert.equal(r.body.data!.userId, userId, 'userId 불일치');
            assert.equal(r.body.data!.nickname, nickname, 'nickname 불일치');
        }],

        ['토큰 없음 /me → 401 AUTH_REQUIRED', async () =>
        {
            const r = await getMe();
            assert.equal(r.status, 401, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'AUTH_REQUIRED');
        }],

        ['옛 토큰 /heartbeat → 401 SESSION_SUPERSEDED', async () =>
        {
            const r = await get('/api/auth/heartbeat', token1);
            assert.equal(r.status, 401, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'SESSION_SUPERSEDED');
        }],

        ['새 토큰 /heartbeat → 200 valid', async () =>
        {
            const r = await get('/api/auth/heartbeat', token2);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            assert.equal(r.body.data?.valid, true);
        }],

        ['게임중 재로그인 → DS kick 폴링 목록에 등장 + 토큰 검증', async () =>
        {
            // 별도 계정을 매치중이라고 가정(roster 시드) → 재로그인 시 emitSuperseded가 markKick 발화.
            const s2 = randomBytes(3).toString('hex');
            const kLoginId = `sessk${s2}`;
            const reg2 = await post('/api/auth/register', { loginId: kLoginId, password: PASSWORD, nickname: `SessK${s2}` });
            assert.equal(reg2.body.ok, true, JSON.stringify(reg2.body));
            const uid2 = reg2.body.data!.userId as number;
            await post('/api/auth/login', { loginId: kLoginId, password: PASSWORD });

            const mid = `ksim-${s2}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid,
                serverToken: stk,
                mapName: 'default map',
                startedAt: Date.now(),
                players: [{ userId: uid2, nickname: `SessK${s2}`, joinToken: 'kjoin' }],
            });

            // 재로그인(세션 대체) → markKick(mid, uid2) (동기: 로그인 응답 시점엔 이미 표시됨)
            const l2 = await post('/api/auth/login', { loginId: kLoginId, password: PASSWORD });
            assert.equal(l2.body.ok, true, JSON.stringify(l2.body));

            const kres = await get(`/api/match/${mid}/kicks`, stk);
            assert.equal(kres.status, 200, JSON.stringify(kres.body));
            assert.deepEqual((kres.body.data as { userIds: number[] }).userIds, [uid2]);

            const kbad = await get(`/api/match/${mid}/kicks`, 'wrong-token');
            assert.equal(kbad.status, 403, JSON.stringify(kbad.body));
            assert.equal(kbad.body.error?.code, 'INVALID_SERVER_TOKEN');
        }],

        ['옛 토큰 WS 업그레이드 → 401 거절', async () =>
        {
            const r = await probeWs(wsUrl, token1);
            assert.equal(r.open, false, '옛 토큰으로 WS가 연결됨');
            assert.equal(r.status, 401, `기대 401, 실제 ${r.status}`);
        }],

        ['새 토큰 WS 업그레이드 → 연결', async () =>
        {
            const r = await probeWs(wsUrl, token2);
            assert.equal(r.open, true, '새 토큰으로 WS 연결 실패');
        }],

        ['재로그인 시 큐 대기 중이던 기존 WS 즉시 종료', async () =>
        {
            // 현재 유효한 token2로 연결해 큐에 들어간 뒤, 3차 로그인이 이 소켓을 끊어야 한다.
            const ws = await openWs(wsUrl, token2);
            try
            {
                ws.send(JSON.stringify({ type: 'queue:join' }));
                const joined = await nextMessage(ws);
                assert.equal(joined.type, 'queue:joined', JSON.stringify(joined));

                // close 리스너를 로그인 전에 걸어 레이스 없이 종료를 포착한다.
                const closed = new Promise<void>((resolve) => ws.once('close', () => resolve()));
                const login3 = await post('/api/auth/login', { loginId, password: PASSWORD });
                assert.equal(login3.body.ok, true, `3차 login 실패: ${JSON.stringify(login3.body)}`);

                await withTimeout(closed, 3000, '재로그인 후에도 기존 WS가 닫히지 않음');
            }
            finally
            {
                ws.terminate();
            }
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

    matchWs.stop();
    server.close();
    await closePool();

    if (failed > 0)
    {
        process.exit(1);
    }
}

void main();
