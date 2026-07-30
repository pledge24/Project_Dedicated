// 매치 결과 엔드포인트 통합 하네스(인프로세스). 앱을 같은 프로세스에 띄워 roster를 직접 시드한다
// (roster 캐시는 인메모리라 별도 프로세스에선 못 건드림). 언리얼 DS 없이 F5a를 끝까지 검증.
// 시드한 match_rosters 행은 다른 시드 데이터(users·matches)와 마찬가지로 남긴다 — roster.sweep이
// DS 최대 수명 경과분을 자동 청소하므로 별도 정리가 필요 없다.
// 실행: npm run match:result-sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import type { ResultSetHeader, RowDataPacket } from 'mysql2';
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
    await roster.register({
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

        ['탈주 즉시 정산(POST /leaver) → 최하위 확정값·결과시 프로필 skip·완주자끼리 ELO', async () =>
        {
            const s = randomBytes(3).toString('hex');
            const us = await seedUsers('lv', s, 4); // 모두 1000점
            const mid = `lv-${s}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid, serverToken: stk, mapName: MAP, startedAt: Date.now(),
                players: us.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `lj${i}` })),
            });

            // 1) us[3] 탈주 즉시 정산 → base[꼴등](-35) - leaverPenalty. ELO 무관 확정값.
            const expectedDelta = -35 - config.match.leaverPenalty;
            const leave = await post(`/api/match/${mid}/leaver`, { userId: us[3].userId }, stk);
            assert.equal(leave.status, 200, JSON.stringify(leave.body));
            assert.equal(leave.body.data!.scoreDelta, expectedDelta, `정산 delta=${leave.body.data!.scoreDelta} 기대 ${expectedDelta}`);
            const afterSettle = await selectScore([us[3].userId]);
            assert.equal(afterSettle.get(us[3].userId), 1000 + expectedDelta, '정산 후 프로필 점수 불일치');

            // 2) 재정산 요청은 멱등 — 같은 값 반환, 추가 하락 없음.
            const again = await post(`/api/match/${mid}/leaver`, { userId: us[3].userId }, stk);
            assert.equal(again.body.data!.scoreDelta, expectedDelta, '재정산이 멱등 아님');
            assert.equal((await selectScore([us[3].userId])).get(us[3].userId), 1000 + expectedDelta, '재정산이 점수를 또 깎음');

            // 3) 매치 종료 결과: us[3]=left·꼴등(4), 완주자 us[0..2]=1,2,3
            const body = {
                matchId: mid, mapName: MAP, durationSec: 60, endReason: 'winner',
                results: us.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i === 3 ? 4 : i + 1, livesLeft: 0, left: i === 3 })),
            };
            const r = await post('/api/match/result', body, stk);
            assert.equal(r.status, 200, JSON.stringify(r.body));

            // 탈주자 프로필 재갱신 안 됨(정산값 유지) + abandoned 컬럼 1, 완주자 0
            assert.equal((await selectScore([us[3].userId])).get(us[3].userId), 1000 + expectedDelta, '결과 저장이 탈주자 프로필을 재갱신함(중복)');
            const flags = await selectAbandoned(us.map((u) => u.userId));
            assert.equal(flags.get(us[3].userId), 1, '탈주자 abandoned=1 아님');
            assert.equal(flags.get(us[0].userId), 0, '완주자 abandoned=0 아님');

            // 완주자끼리 ELO — 1위(us[0])는 양수, 결과의 탈주자 delta는 정산값과 동일.
            const ps = r.body.data!.participants as Array<{ userId: number; scoreDelta: number }>;
            assert.ok(ps.find((p) => p.userId === us[0].userId)!.scoreDelta > 0, '완주 1위 delta 양수 아님');
            assert.equal(ps.find((p) => p.userId === us[3].userId)!.scoreDelta, expectedDelta, '결과의 탈주자 delta가 정산값 아님');
        }],

        ['탈주 정산 유실 — /leaver 없이 /result만 와도 결과가 패널티 적용(0 escape 아님)', async () =>
        {
            const s = randomBytes(3).toString('hex');
            const us = await seedUsers('esc', s, 4); // 모두 1000점
            const mid = `esc-${s}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid, serverToken: stk, mapName: MAP, startedAt: Date.now(),
                players: us.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `ej${i}` })),
            });

            // /leaver를 전혀 안 보내고 곧장 /result — us[2],us[3] 탈주(left). DS 셧다운으로 /leaver 유실된 케이스.
            const expectedDelta = -35 - config.match.leaverPenalty;
            const body = {
                matchId: mid, mapName: MAP, durationSec: 60, endReason: 'winner',
                results: us.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i <= 1 ? i + 1 : 4, livesLeft: 0, left: i >= 2 })),
            };
            const r = await post('/api/match/result', body, stk);
            assert.equal(r.status, 200, JSON.stringify(r.body));

            // 두 탈주자 모두 프로필·결과에 패널티 반영(escape 0 아님) + abandoned=1.
            const scores = await selectScore([us[2].userId, us[3].userId]);
            assert.equal(scores.get(us[2].userId), 1000 + expectedDelta, '탈주자1 프로필 패널티 미반영(escape)');
            assert.equal(scores.get(us[3].userId), 1000 + expectedDelta, '탈주자2 프로필 패널티 미반영(escape)');
            const ps = r.body.data!.participants as Array<{ userId: number; scoreDelta: number }>;
            assert.equal(ps.find((p) => p.userId === us[2].userId)!.scoreDelta, expectedDelta, '결과 탈주자1 delta escape');
            assert.equal(ps.find((p) => p.userId === us[3].userId)!.scoreDelta, expectedDelta, '결과 탈주자2 delta escape');
            const flags = await selectAbandoned([us[2].userId, us[3].userId]);
            assert.equal(flags.get(us[2].userId), 1, '탈주자1 abandoned=1 아님');
            assert.equal(flags.get(us[3].userId), 1, '탈주자2 abandoned=1 아님');
        }],

        ['탈주 경합 — /leaver 다수와 /result 동시 발사해도 각 탈주자 정확히 1회 정산', async () =>
        {
            const s = randomBytes(3).toString('hex');
            const us = await seedUsers('rc', s, 4); // 모두 1000점
            const mid = `rc-${s}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid, serverToken: stk, mapName: MAP, startedAt: Date.now(),
                players: us.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `rj${i}` })),
            });

            // 신고 시나리오: 3명 탈주 + 1명 승. 마지막 탈주의 /leaver가 /result와 경합하던 지점.
            const expectedDelta = -35 - config.match.leaverPenalty;
            const body = {
                matchId: mid, mapName: MAP, durationSec: 60, endReason: 'winner',
                results: us.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i === 0 ? 1 : 4, livesLeft: 0, left: i >= 1 })),
            };
            const [, , , res] = await Promise.all([
                post(`/api/match/${mid}/leaver`, { userId: us[1].userId }, stk),
                post(`/api/match/${mid}/leaver`, { userId: us[2].userId }, stk),
                post(`/api/match/${mid}/leaver`, { userId: us[3].userId }, stk),
                post('/api/match/result', body, stk),
            ]);
            assert.equal(res.status, 200, `result: ${JSON.stringify(res.body)}`);

            // 세 탈주자 모두 정확히 1회(=expectedDelta). 0(escape)도 -130(이중)도 아님.
            const scores = await selectScore([us[1].userId, us[2].userId, us[3].userId]);
            for (const u of [us[1], us[2], us[3]])
            {
                assert.equal(scores.get(u.userId), 1000 + expectedDelta, `탈주자 ${u.userId} 정산 1회 아님(=${scores.get(u.userId)})`);
            }
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
            await roster.register({
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

        ['봇전 — 완주자 4명(휴먼+봇3) ELO로 휴먼 반영, 봇은 DB 미기록', async () =>
        {
            const s = randomBytes(3).toString('hex');
            const hu = (await seedUsers('bf', s, 1))[0]; // 1000점
            const mid = `bf-${s}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid, serverToken: stk, mapName: MAP, startedAt: Date.now(),
                players: [
                    { userId: hu.userId, nickname: hu.nickname, joinToken: 'bfjoin' },
                    { userId: -1, nickname: 'Bot Arden', joinToken: '', bot: true, rating: 1000 },
                    { userId: -2, nickname: 'Bot Luna', joinToken: '', bot: true, rating: 1000 },
                    { userId: -3, nickname: 'Bot Milo', joinToken: '', bot: true, rating: 1000 },
                ],
            });
            const body = {
                matchId: mid, mapName: MAP, durationSec: 90, endReason: 'winner',
                results: [
                    { userId: hu.userId, slotIndex: 0, placement: 1, livesLeft: 2 },
                    { userId: -1, slotIndex: 1, placement: 2, livesLeft: 0 },
                    { userId: -2, slotIndex: 2, placement: 3, livesLeft: 0 },
                    { userId: -3, slotIndex: 3, placement: 4, livesLeft: 0 },
                ],
            };
            const r = await post('/api/match/result', body, stk);
            assert.equal(r.status, 200, JSON.stringify(r.body));

            // 응답·DB엔 실제 유저만. 봇은 어디에도 기록 안 됨.
            const ps = r.body.data!.participants as Array<{ userId: number; scoreDelta: number; scoreAfter: number }>;
            assert.equal(ps.length, 1, `봇전 응답은 실제 유저 1명이어야 함(실제 ${ps.length})`);
            assert.equal(ps[0].userId, hu.userId);
            assert.ok(ps[0].scoreDelta > 0, `1위 휴먼 delta=${ps[0].scoreDelta} 양수 아님(봇3 대상 ELO+배점)`);
            assert.equal(await countParticipants(mid), 1, '봇전 match_participants는 휴먼 1행이어야 함');
            assert.equal((await selectScore([hu.userId])).get(hu.userId), 1000 + ps[0].scoreDelta, '휴먼 프로필 점수 미반영');
        }],

        ['봇전 — 봇 단독승이면 winner_user_id NULL(FK 보호)', async () =>
        {
            const s = randomBytes(3).toString('hex');
            const hu = (await seedUsers('bw', s, 1))[0];
            const mid = `bw-${s}-${randomBytes(4).toString('hex')}`;
            const stk = randomBytes(24).toString('base64url');
            await roster.register({
                matchId: mid, serverToken: stk, mapName: MAP, startedAt: Date.now(),
                players: [
                    { userId: hu.userId, nickname: hu.nickname, joinToken: 'bwjoin' },
                    { userId: -1, nickname: 'Bot Arden', joinToken: '', bot: true, rating: 1000 },
                    { userId: -2, nickname: 'Bot Luna', joinToken: '', bot: true, rating: 1000 },
                    { userId: -3, nickname: 'Bot Milo', joinToken: '', bot: true, rating: 1000 },
                ],
            });
            const body = {
                matchId: mid, mapName: MAP, durationSec: 90, endReason: 'winner',
                results: [
                    { userId: hu.userId, slotIndex: 0, placement: 2, livesLeft: 0 },
                    { userId: -1, slotIndex: 1, placement: 1, livesLeft: 3 }, // 봇 단독 1위
                    { userId: -2, slotIndex: 2, placement: 3, livesLeft: 0 },
                    { userId: -3, slotIndex: 3, placement: 4, livesLeft: 0 },
                ],
            };
            const r = await post('/api/match/result', body, stk);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            assert.equal(await selectWinner(mid), null, '봇 단독승인데 winner_user_id가 NULL 아님');
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

/** HTTP register(rate-limit) 우회 — users+player_profiles를 DB에 직접 넣고 시작 점수 1000을 보장. */
async function seedUsers(label: string, s: string, n: number): Promise<Array<{ userId: number; nickname: string }>>
{
    const out: Array<{ userId: number; nickname: string }> = [];
    for (let i = 0; i < n; i++)
    {
        const nickname = `${label.toUpperCase()}${s}${i}`;
        const [ins] = await getPool().execute<ResultSetHeader>(
            'INSERT INTO users (login_id, password_hash, nickname) VALUES (?, ?, ?)',
            [`${label}${s}${i}`, 'seed', nickname]
        );
        const userId = ins.insertId;
        await getPool().execute('INSERT INTO player_profiles (user_id) VALUES (?)', [userId]);
        out.push({ userId, nickname });
    }

    return out;
}

/** 여러 유저의 player_profiles.score 조회. */
async function selectScore(userIds: number[]): Promise<Map<number, number>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await getPool().query<RowDataPacket[]>(
        `SELECT user_id, score FROM player_profiles WHERE user_id IN (${placeholders})`,
        userIds
    );

    const result = new Map<number, number>();
    for (const row of rows)
    {
        result.set(Number(row.user_id), Number(row.score));
    }

    return result;
}

/** 해당 매치(client_match_id)의 match_participants 행 수 — 봇전에서 봇 미기록 검증용. */
async function countParticipants(matchId: string): Promise<number>
{
    const [rows] = await getPool().query<RowDataPacket[]>(
        'SELECT COUNT(*) AS n FROM match_participants mp JOIN matches m ON mp.match_id = m.id WHERE m.client_match_id = ?',
        [matchId]
    );

    return Number(rows[0].n);
}

/** 해당 매치의 winner_user_id(NULL이면 null) — 봇 단독승 보정 검증용. */
async function selectWinner(matchId: string): Promise<number | null>
{
    const [rows] = await getPool().query<RowDataPacket[]>(
        'SELECT winner_user_id FROM matches WHERE client_match_id = ?',
        [matchId]
    );

    return rows.length > 0 && rows[0].winner_user_id !== null ? Number(rows[0].winner_user_id) : null;
}

/** 여러 유저의 match_participants.abandoned(=left) 플래그 조회(각 유저가 매치 1개뿐인 시나리오 전제). */
async function selectAbandoned(userIds: number[]): Promise<Map<number, number>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await getPool().query<RowDataPacket[]>(
        `SELECT user_id, abandoned FROM match_participants WHERE user_id IN (${placeholders})`,
        userIds
    );

    const result = new Map<number, number>();
    for (const row of rows)
    {
        result.set(Number(row.user_id), Number(row.abandoned));
    }

    return result;
}

void main();
