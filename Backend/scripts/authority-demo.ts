// 서버 권위 시연 — "로그인한 진짜 유저도 자기 매치 결과를 조작할 수 없다"를 한 명령으로 보여준다.
//
// 시연의 핵심은 대비다. 같은 JWT로:
//   · GET /api/ranking          → 200  (토큰은 분명히 유효하다)
//   · POST /api/match/result    → 403  (그래도 결과는 못 쓴다)
// 토큰이 만료됐거나 잘못돼서 막힌 게 아니라, 이 엔드포인트의 주체가 아예 다르다는 뜻이다.
//
// matchId를 지어내면 roster 조회에서 404가 먼저 나므로 시연이 성립하지 않는다.
// 그래서 실제로 큐에 들어가 매치를 하나 잡고, 그 매치의 진짜 matchId로 공격한다.
//
// 실행: npm run demo:authority   (백엔드 가동 필요. MATCH_DS_ENABLED는 true/false 무관)
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { WebSocket } from 'ws';

const BASE_URL = process.env.MATCH_BOTS_URL ?? 'http://127.0.0.1:3000';
const WS_URL = BASE_URL.replace(/^http/, 'ws') + '/ws/match';
const PASSWORD = 'demopass123';
const MATCH_WAIT_MS = 90_000;

interface Envelope
{
    ok: boolean;
    data?: Record<string, unknown>;
    error?: { code: string; message: string };
}

interface Attempt
{
    status: number;
    body: Envelope;
}

const line = (): void => console.log('─'.repeat(72));

async function main(): Promise<void>
{
    console.log('\n서버 권위 시연 — 클라이언트는 매치 결과를 쓸 수 없다');
    line();

    const { userId, nickname, jwt } = await seedPlayer();
    console.log(`① 정상 로그인   ${nickname} (userId=${userId})`);
    console.log(`   JWT: ${jwt.slice(0, 32)}…`);

    // 이 토큰이 유효하다는 증거 — 같은 토큰으로 보호된 API가 통과한다.
    const ranking = await request('GET', '/api/ranking?limit=1', { jwt });
    console.log(`② 같은 토큰으로 랭킹 조회 → ${ranking.status} ${ranking.body.ok ? 'OK' : ranking.body.error?.code}`);
    assert.equal(ranking.status, 200, '사전 조건: JWT가 유효해야 시연이 성립한다');

    console.log('③ 큐 입장 — 실제 매치를 하나 잡는다(봇전 대기 포함)');
    const match = await joinQueueAndWaitForMatch(jwt);
    console.log(`   매치 성사   matchId=${match.matchId}  server=${match.host}:${match.port}`);
    console.log(`   이 유저는 이 매치의 진짜 참가자다 — 남의 매치를 찌르는 게 아니다.`);

    line();
    console.log('④ 공격: 자기 자신을 1등으로 조작해 결과 보고');

    const forged = {
        matchId: match.matchId,
        mapName: 'default map',
        durationSec: 1,
        endReason: 'winner',
        results: [{ userId, slotIndex: 0, placement: 1, livesLeft: 3 }],
    };

    const noAuth = await request('POST', '/api/match/result', { body: forged });
    report('인증 없이', noAuth, 401, 'SERVER_AUTH_REQUIRED');

    const withJwt = await request('POST', '/api/match/result', { jwt, body: forged });
    report('본인 JWT로', withJwt, 403, 'INVALID_SERVER_TOKEN');

    const guessed = randomBytes(24).toString('base64url');
    const withGuess = await request('POST', '/api/match/result', { bearer: guessed, body: forged });
    report('토큰 추측', withGuess, 403, 'INVALID_SERVER_TOKEN');

    line();
    console.log('결론');
    console.log('  · 같은 토큰이 랭킹 조회(200)는 되고 결과 보고(403)는 안 된다');
    console.log('  · 막은 근거는 "로그인 여부"가 아니라 "이 요청의 주체가 DS인가"다');
    console.log('  · serverToken은 매치별로 발급돼 DS 프로세스에만 전달된다');
    console.log('    (커맨드라인이 아니라 임시 파일로 — DS가 읽는 즉시 삭제한다)\n');
}

/** 시연용 계정 1개 등록 + 로그인. */
async function seedPlayer(): Promise<{ userId: number; nickname: string; jwt: string }>
{
    const suffix = randomBytes(3).toString('hex');
    const loginId = `demo${suffix}`;
    const nickname = `Demo${suffix}`;

    const reg = await request('POST', '/api/auth/register', { body: { loginId, password: PASSWORD, nickname } });
    assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);

    const login = await request('POST', '/api/auth/login', { body: { loginId, password: PASSWORD } });
    assert.equal(login.body.ok, true, `login 실패: ${JSON.stringify(login.body)}`);

    return {
        userId: reg.body.data!.userId as number,
        nickname,
        jwt: login.body.data!.token as string,
    };
}

/** 매칭 WS로 큐 입장 → match:found 수신까지 대기. 상대가 없으면 봇전으로 잡힌다. */
function joinQueueAndWaitForMatch(jwt: string): Promise<{ matchId: string; host: string; port: number }>
{
    return new Promise((resolve, reject) =>
    {
        const ws = new WebSocket(WS_URL, { headers: { Authorization: `Bearer ${jwt}` } });
        const timer = setTimeout(() =>
        {
            ws.close();
            reject(new Error(`매치가 ${MATCH_WAIT_MS / 1000}초 안에 안 잡혔다 — MATCH_BOT_FILL_ENABLED를 확인`));
        }, MATCH_WAIT_MS);

        ws.on('open', () => ws.send(JSON.stringify({ type: 'queue:join' })));
        ws.on('error', (err) =>
        {
            clearTimeout(timer);
            reject(err);
        });

        ws.on('message', (raw) =>
        {
            const msg = JSON.parse(raw.toString()) as { type: string; data?: Record<string, unknown> };
            if (msg.type !== 'match:found')
            {
                return;
            }

            clearTimeout(timer);
            const server = msg.data!.server as { host: string; port: number };
            // 클라는 여기서 DS로 travel한다. 시연에서는 들어가지 않고 백엔드를 직접 찌른다.
            ws.close();
            resolve({ matchId: msg.data!.matchId as string, host: server.host, port: server.port });
        });
    });
}

async function request(method: string, path: string, opts: { jwt?: string; bearer?: string; body?: unknown } = {}): Promise<Attempt>
{
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    const token = opts.bearer ?? opts.jwt;
    if (token)
    {
        headers.Authorization = `Bearer ${token}`;
    }

    const res = await fetch(BASE_URL + path, {
        method,
        headers,
        body: opts.body === undefined ? undefined : JSON.stringify(opts.body),
    });

    return { status: res.status, body: (await res.json()) as Envelope };
}

/** 한 시도의 결과를 시연용 한 줄로. 기대와 다르면 즉시 실패시킨다(영상에 잘못된 화면이 남지 않게). */
function report(label: string, attempt: Attempt, expectedStatus: number, expectedCode: string): void
{
    const mark = attempt.status === expectedStatus && attempt.body.error?.code === expectedCode ? '거부됨' : '예상과 다름!';
    console.log(`   ${label.padEnd(12)} → ${attempt.status} ${attempt.body.error?.code ?? '(코드 없음)'}  ${mark}`);
    console.log(`   ${' '.repeat(12)}   "${attempt.body.error?.message ?? ''}"`);

    assert.equal(attempt.status, expectedStatus, `${label}: 상태 코드가 기대와 다르다`);
    assert.equal(attempt.body.error?.code, expectedCode, `${label}: 에러 코드가 기대와 다르다`);
}

main().catch((err) =>
{
    console.error('\n시연 실패:', err instanceof Error ? err.message : err);
    process.exit(1);
});
