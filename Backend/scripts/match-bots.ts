// 매칭 봇 채우기 — 테스트 유저 N명을 register/login 후 WS로 queue:join.
// PIE 클라 1명과 합쳐 4인 매칭을 성사시키는 용도(또는 봇만으로 서버/프로토콜 독립 검증).
// 실행: npm run match:bots [count]   (기본 3). 종료: Ctrl+C.
// 주의: login 레이트리밋 기본 5/분 — 봇 5명 초과 시 RATE_LIMIT_LOGIN_MAX 올리거나 잠시 대기.
import { WebSocket } from 'ws';

const BASE_URL = process.env.MATCH_BOTS_URL ?? 'http://127.0.0.1:3000';
const WS_URL = BASE_URL.replace(/^http/, 'ws') + '/ws/match'; // http→ws, https→wss
const COUNT = Math.max(1, Number(process.argv[2] ?? '3') || 3);
const PASSWORD = 'botpass123';

interface Envelope
{
    ok: boolean;
    data?: Record<string, unknown>;
    error?: { code: string; message: string };
}

async function post(path: string, body: unknown): Promise<Envelope>
{
    const res = await fetch(BASE_URL + path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    });
    return res.json() as Promise<Envelope>;
}

/** 봇 계정 확보 → JWT 반환. 이미 있으면 register 409 무시하고 login. */
async function ensureToken(loginId: string, nickname: string): Promise<string>
{
    const reg = await post('/api/auth/register', { loginId, password: PASSWORD, nickname });
    if (!reg.ok && reg.error?.code !== 'DUPLICATE_LOGIN_ID' && reg.error?.code !== 'DUPLICATE_NICKNAME')
    {
        console.warn(`  [${loginId}] register 경고: ${reg.error?.code} ${reg.error?.message ?? ''}`);
    }

    const login = await post('/api/auth/login', { loginId, password: PASSWORD });
    const token = typeof login.data?.token === 'string' ? login.data.token : '';
    if (!token)
    {
        throw new Error(`[${loginId}] login 실패: ${login.error?.code ?? 'no token'} ${login.error?.message ?? ''}`);
    }
    return token;
}

function connectBot(loginId: string, token: string): void
{
    const ws = new WebSocket(WS_URL, { headers: { Authorization: `Bearer ${token}` } });

    ws.on('open', () =>
    {
        ws.send(JSON.stringify({ type: 'queue:join' }));
        console.log(`  [${loginId}] WS 연결 + queue:join`);
    });

    ws.on('message', (raw: Buffer) =>
    {
        let msg: { type?: string; data?: { players?: unknown[] } };
        try { msg = JSON.parse(raw.toString()); }
        catch { return; }

        if (msg.type === 'match:found')
        {
            const n = Array.isArray(msg.data?.players) ? msg.data.players.length : 0;
            console.log(`  [${loginId}] ★ match:found (${n}명)`);
        }
        else
        {
            console.log(`  [${loginId}] ← ${msg.type}`);
        }
    });

    ws.on('close', (code: number) => console.log(`  [${loginId}] WS 종료 (code=${code})`));
    ws.on('error', (err: Error) => console.error(`  [${loginId}] WS 에러: ${err.message}`));
}

async function main(): Promise<void>
{
    console.log(`매칭 봇 ${COUNT}명 → ${WS_URL}`);
    for (let i = 1; i <= COUNT; i++)
    {
        const loginId = `matchbot${i}`;
        const nickname = `MatchBot${i}`;
        try
        {
            const token = await ensureToken(loginId, nickname);
            connectBot(loginId, token);
        }
        catch (err)
        {
            console.error(err instanceof Error ? err.message : err);
        }
    }
    console.log('봇 대기 중 — Ctrl+C로 종료');
}

void main();
