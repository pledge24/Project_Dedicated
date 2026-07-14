// 매치 결과 엔드포인트 통합 하네스(인프로세스). 앱을 같은 프로세스에 띄워 roster를 직접 시드한다
// (roster는 인메모리라 별도 프로세스에선 못 건드림). 언리얼 DS 없이 F5a를 끝까지 검증.
// 실행: npm run match:result-sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import type { RowDataPacket } from 'mysql2';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import type { AddressInfo } from 'node:net';

import buildApp from '../src/app.js';
import { config } from '../src/common/config.js';
import { closePool, getPool } from '../src/common/db.js';
import * as roster from '../src/match/roster.js';

const PASSWORD = 'resulttest123';
const MAP = 'default map'; // DS가 보고하는 논리 맵 이름(레벨 경로 아님)과 동일 형태

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
        players: users.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `simjoin${i}` })),
    });

    const resultBody = {
        matchId,
        mapName: MAP,
        durationSec: 123,
        endReason: 'winner',
        results: users.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i + 1, livesLeft: i === 0 ? 2 : 0 })),
    };

    // 레벨업 검증용 사전 시드: 1위(users[0])의 exp를 경계 근처로 → 1위 획득 +100이면 1000을 넘겨 Lv.2.
    await getPool().execute('UPDATE player_profiles SET exp = 950 WHERE user_id = ?', [users[0].userId]);

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

        ['slotIndex 범위 밖 → 400', async () =>
        {
            const bad = { ...resultBody, results: resultBody.results.map((e, i) => i === 0 ? { ...e, slotIndex: 4 } : e) };
            const r = await post('/api/match/result', bad, serverToken);
            assert.equal(r.status, 400, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'INVALID_RESULT');
        }],

        ['slotIndex 중복 → 400', async () =>
        {
            const bad = { ...resultBody, results: resultBody.results.map((e) => ({ ...e, slotIndex: 0 })) };
            const r = await post('/api/match/result', bad, serverToken);
            assert.equal(r.status, 400, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'INVALID_RESULT');
        }],

        ['정상 제출 → 200 + 인플레(sum>0) + 1·2위>0>4위', async () =>
        {
            const r = await post('/api/match/result', resultBody, serverToken);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const ps = r.body.data!.participants as Array<{ placement: number; scoreDelta: number; scoreAfter: number }>;
            assert.equal(ps.length, 4);
            const sum = ps.reduce((a, p) => a + p.scoreDelta, 0);
            assert.ok(sum > 0, `인플레 위반 sum=${sum} (매치 후 합이 더 커야 함)`);
            const first = ps.find((p) => p.placement === 1)!;
            const second = ps.find((p) => p.placement === 2)!;
            const last = ps.find((p) => p.placement === 4)!;
            assert.ok(first.scoreDelta > 0, `1위 scoreDelta=${first.scoreDelta} 양수 아님`);
            assert.ok(second.scoreDelta > 0, `2위 scoreDelta=${second.scoreDelta} 양수 아님`);
            assert.ok(last.scoreDelta < 0, `4위 scoreDelta=${last.scoreDelta} 음수 아님`);
            assert.equal(first.scoreAfter, 1000 + first.scoreDelta);
        }],

        ['레벨업 — exp 950 + 1위 100 = 1050 → Lv.2', async () =>
        {
            const login = await post('/api/auth/login', { loginId: `rt${suffix}0`, password: PASSWORD });
            assert.equal(login.body.ok, true, JSON.stringify(login.body));
            const data = login.body.data as { exp: number; level: number };
            assert.equal(data.exp, 1050, `exp=${data.exp} (기대 1050)`);
            assert.equal(data.level, 2, `level=${data.level} (기대 2)`);
        }],

        ['중복 제출 → 409', async () =>
        {
            const r = await post('/api/match/result', resultBody, serverToken);
            assert.equal(r.status, 409, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'RESULT_ALREADY_SUBMITTED');
        }],

        ['±0 점수 매치는 score_updated_at 유지 (실변동은 갱신)', async () =>
        {
            // 새 4계정 + 새 매치. zu[3]을 하한 점수로 시드 + 4위 배치 → scoreAfter=floor, scoreDelta=0.
            const s2 = randomBytes(3).toString('hex');
            const zu: Array<{ userId: number; nickname: string }> = [];
            for (let i = 0; i < 4; i++)
            {
                const nickname = `Z${s2}${i}`;
                const reg = await post('/api/auth/register', { loginId: `z${s2}${i}`, password: PASSWORD, nickname });
                assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);
                zu.push({ userId: reg.body.data!.userId as number, nickname });
            }
            // zu[3]을 하한 점수로 → 4위(기본배점 -35 + 음수 ELO)라도 clamp되어 delta 0.
            await getPool().execute('UPDATE player_profiles SET score = ? WHERE user_id = ?', [config.match.scoreFloor, zu[3].userId]);

            const before = await selectScoreUpdatedAt([zu[0].userId, zu[3].userId]);

            const mid = `zsim-${s2}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            roster.register({
                matchId: mid,
                serverToken: stk,
                mapName: MAP,
                startedAt: Date.now(),
                players: zu.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `zjoin${i}` })),
            });
            const body = {
                matchId: mid,
                mapName: MAP,
                durationSec: 60,
                endReason: 'winner',
                results: zu.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i + 1, livesLeft: i === 0 ? 2 : 0 })),
            };
            const r = await post('/api/match/result', body, stk);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const ps = r.body.data!.participants as Array<{ userId: number; scoreDelta: number }>;
            const zeroP = ps.find((p) => p.userId === zu[3].userId)!;
            assert.equal(zeroP.scoreDelta, 0, `4위(하한) scoreDelta=${zeroP.scoreDelta} (기대 0)`);

            const after = await selectScoreUpdatedAt([zu[0].userId, zu[3].userId]);
            // ±0 유저: 시점 유지. 실변동 유저(1위, +점수): input.endedAt로 갱신 → 값이 달라짐.
            assert.equal(after.get(zu[3].userId), before.get(zu[3].userId), '±0인데 score_updated_at이 바뀜');
            assert.notEqual(after.get(zu[0].userId), before.get(zu[0].userId), '실변동인데 score_updated_at이 안 바뀜');
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

/** 여러 유저의 score_updated_at을 문자열(밀리초)로 조회. Date 객체 참조 비교 함정 회피. */
async function selectScoreUpdatedAt(userIds: number[]): Promise<Map<number, string>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await getPool().query<RowDataPacket[]>(
        'SELECT user_id, DATE_FORMAT(score_updated_at, \'%Y-%m-%d %H:%i:%s.%f\') AS sua ' +
        `FROM player_profiles WHERE user_id IN (${placeholders})`,
        userIds
    );

    const result = new Map<number, string>();
    for (const row of rows)
    {
        result.set(Number(row.user_id), String(row.sua));
    }

    return result;
}

void main();
