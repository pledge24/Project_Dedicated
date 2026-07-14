// 랭킹 조회 엔드포인트 통합 하네스(인프로세스). 앱을 같은 프로세스에 띄우고
// 시드 유저 5명을 현재 MAX(score) 위로 올려 '상위 밴드'를 점유 → 기존 데이터와 격리해 검증.
// 실행: npm run ranking:sim   (MySQL 가동 + Backend/.env 필요. 임의 빈 포트로 listen.)
import type { RowDataPacket } from 'mysql2';
import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import type { AddressInfo } from 'node:net';

import buildApp from '../src/app.js';
import { closePool, getPool } from '../src/common/db.js';

const PASSWORD = 'ranktest123';

interface ApiResult
{
    status: number;
    body: { ok: boolean; data?: Record<string, unknown>; error?: { code: string; message: string } };
}

interface RankEntry
{
    rank: number;
    userId: number;
    nickname: string;
    score: number;
    level: number;
    wins: number;
    losses: number;
    matchesPlayed: number;
}

interface MaxRow extends RowDataPacket
{
    m: number;
}

async function main(): Promise<void>
{
    const app = buildApp();
    const server = app.listen(0);
    await new Promise<void>((resolve) => server.once('listening', () => resolve()));
    const { port } = server.address() as AddressInfo;
    const base = `http://127.0.0.1:${port}`;

    async function post(path: string, body: unknown): Promise<ApiResult>
    {
        const res = await fetch(base + path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });

        return { status: res.status, body: (await res.json()) as ApiResult['body'] };
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

    // 1. 시드 유저 5명 등록 → userId 확보(AUTO_INCREMENT라 등록 순 = userId 오름차순)
    const suffix = randomBytes(3).toString('hex');
    const users: Array<{ userId: number; loginId: string; nickname: string }> = [];
    for (let i = 0; i < 5; i++)
    {
        const loginId = `rk${suffix}${i}`;
        const nickname = `RK${suffix}${i}`;
        const reg = await post('/api/auth/register', { loginId, password: PASSWORD, nickname });
        assert.equal(reg.body.ok, true, `register 실패: ${JSON.stringify(reg.body)}`);
        users.push({ userId: reg.body.data!.userId as number, loginId, nickname });
    }
    const ids = users.map((u) => u.userId);

    // 2. 현재 최고 점수 위로 시드 점수 배치 → 내 5명이 항상 최상위. U0·U1 동점(tie-break 검증).
    const [maxRows] = await getPool().query<MaxRow[]>('SELECT COALESCE(MAX(score), 0) AS m FROM player_profiles');
    const top = Number(maxRows[0].m) + 1000;
    // [score, level, wins, losses, matches_played]  — U0는 매핑 검증용 특수값.
    const seed: Array<[number, number, number, number, number]> = [
        [top,       4, 9, 3, 12], // U0
        [top,       2, 4, 4, 8],  // U1 (U0와 동점 → userId 작은 U0 먼저)
        [top - 100, 2, 3, 5, 8],  // U2
        [top - 200, 1, 1, 6, 7],  // U3
        [top - 300, 1, 0, 7, 7],  // U4
    ];
    for (let i = 0; i < users.length; i++)
    {
        const [score, level, wins, losses, played] = seed[i];
        await getPool().execute(
            'UPDATE player_profiles SET score = ?, level = ?, wins = ?, losses = ?, matches_played = ? WHERE user_id = ?',
            [score, level, wins, losses, played, users[i].userId]
        );
    }

    // 3. U0 로그인 → JWT
    const login = await post('/api/auth/login', { loginId: users[0].loginId, password: PASSWORD });
    assert.equal(login.body.ok, true, `login 실패: ${JSON.stringify(login.body)}`);
    const token = login.body.data!.token as string;

    const checks: Array<[string, () => Promise<void>]> = [
        ['Authorization 없음 → 401', async () =>
        {
            const r = await get('/api/ranking');
            assert.equal(r.status, 401, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'AUTH_REQUIRED');
        }],

        ['limit=-1 → 400', async () =>
        {
            const r = await get('/api/ranking?limit=-1', token);
            assert.equal(r.status, 400, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'VALIDATION_FAILED');
        }],

        ['limit=abc → 400', async () =>
        {
            const r = await get('/api/ranking?limit=abc', token);
            assert.equal(r.status, 400, JSON.stringify(r.body));
            assert.equal(r.body.error?.code, 'VALIDATION_FAILED');
        }],

        ['limit=9999 → meta.limit=100 클램프', async () =>
        {
            const r = await get('/api/ranking?limit=9999', token);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const meta = r.body.data!.meta as { total: number; limit: number; offset: number };
            assert.equal(meta.limit, 100, `클램프 실패 limit=${meta.limit}`);
        }],

        ['상위 5명 순서 + 동점 tie-break + rank + camelCase 매핑', async () =>
        {
            const r = await get('/api/ranking?limit=5&offset=0', token);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const entries = r.body.data!.entries as RankEntry[];
            assert.equal(entries.length, 5);
            // 내 5명이 최상위 — 순서·rank 검증
            assert.deepEqual(entries.map((e) => e.userId), ids, '상위 5명 순서 불일치');
            assert.deepEqual(entries.map((e) => e.rank), [1, 2, 3, 4, 5], 'rank 불일치');
            // 동점 tie-break: U0·U1 동점이며 userId 작은 U0가 먼저
            assert.equal(entries[0].score, entries[1].score, 'U0·U1 동점 아님');
            assert.ok(entries[0].userId < entries[1].userId, 'tie-break(userId ASC) 위반');
            // camelCase 매핑(U0 특수값)
            assert.equal(entries[0].matchesPlayed, 12, 'matchesPlayed 매핑 오류');
            assert.equal(entries[0].level, 4);
            assert.equal(entries[0].wins, 9);
            assert.equal(entries[0].losses, 3);
            assert.equal(entries[0].score, top);
        }],

        ['페이지네이션 무겹침·연속 rank', async () =>
        {
            const a = await get('/api/ranking?limit=2&offset=0', token);
            const b = await get('/api/ranking?limit=2&offset=2', token);
            const ea = (a.body.data!.entries as RankEntry[]);
            const eb = (b.body.data!.entries as RankEntry[]);
            assert.deepEqual(ea.map((e) => e.userId), [ids[0], ids[1]], 'page A 불일치');
            assert.deepEqual(eb.map((e) => e.userId), [ids[2], ids[3]], 'page B 불일치');
            assert.deepEqual(ea.map((e) => e.rank), [1, 2]);
            assert.deepEqual(eb.map((e) => e.rank), [3, 4]);
            const overlap = ea.some((e) => eb.find((x) => x.userId === e.userId));
            assert.ok(!overlap, '페이지 간 중복 발생');
        }],

        ['meta.total ≥ 5', async () =>
        {
            const r = await get('/api/ranking', token);
            const meta = r.body.data!.meta as { total: number; limit: number; offset: number };
            assert.ok(meta.total >= 5, `total=${meta.total}`);
            assert.equal(meta.limit, 50, '기본 limit=50 아님');
            assert.equal(meta.offset, 0);
        }],

        ['me.rank — 본인 전역 순위(총순서 기준 유일 순위)', async () =>
        {
            // U0: U1과 동점이나 먼저 도달(가입 순) → 유일 1위.
            const r0 = await get('/api/ranking?limit=1', token);
            assert.equal(r0.status, 200, JSON.stringify(r0.body));
            const me0 = r0.body.data!.me as { rank: number };
            assert.equal(me0.rank, 1, `U0 me.rank=${me0?.rank} (기대 1)`);

            // U1: U0와 동점이나 U0가 먼저 도달 → 유일 2위(공동 순위였다면 1).
            const login1 = await post('/api/auth/login', { loginId: users[1].loginId, password: PASSWORD });
            assert.equal(login1.body.ok, true, `U1 login 실패: ${JSON.stringify(login1.body)}`);
            const r1 = await get('/api/ranking', login1.body.data!.token as string);
            const me1 = r1.body.data!.me as { rank: number };
            assert.equal(me1.rank, 2, `U1 me.rank=${me1?.rank} (기대 2 — 유일 순위)`);

            // U2: 위에 U0·U1 2명 → rank 3.
            const login2 = await post('/api/auth/login', { loginId: users[2].loginId, password: PASSWORD });
            assert.equal(login2.body.ok, true, `U2 login 실패: ${JSON.stringify(login2.body)}`);
            const token2 = login2.body.data!.token as string;
            const r2 = await get('/api/ranking', token2);
            const me2 = r2.body.data!.me as { rank: number };
            assert.equal(me2.rank, 3, `U2 me.rank=${me2?.rank} (기대 3)`);
        }],

        ['동점 타이브레이크: 갱신 시점이 user_id를 이김', async () =>
        {
            // U0·U1 동점(top). 가입 순이면 U0가 상위지만, U1의 score_updated_at을 U0보다
            // 이르게 바꾸면 시점 우선 규칙으로 U1(높은 user_id)이 상위가 되어야 한다.
            await getPool().execute(
                "UPDATE player_profiles SET score_updated_at = '2000-01-01 00:00:00.000' WHERE user_id = ?",
                [users[1].userId]
            );
            const r = await get('/api/ranking?limit=2&offset=0', token);
            assert.equal(r.status, 200, JSON.stringify(r.body));
            const entries = r.body.data!.entries as RankEntry[];
            assert.equal(entries[0].userId, users[1].userId, '시점 이른 U1이 최상위 아님(타이브레이크 실패)');
            assert.equal(entries[1].userId, users[0].userId, 'U0가 2위 아님');
            assert.ok(entries[0].userId > entries[1].userId, 'user_id 역전 미확인(높은 user_id가 앞이어야)');
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
