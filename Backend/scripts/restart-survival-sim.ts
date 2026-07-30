// 백엔드 재시작 생존 하네스 — "백엔드가 죽어도 진행 중인 경기는 끝나고 결과가 남는다"를 검증한다.
// 실행: npm run match:restart-sim   (MySQL 가동 + Backend/.env 필요)
//
// 두 가지를 따로 본다:
//   A. DS 수명 분리 — 확정(commit)된 DS는 shutdownUncommitted가 죽이지 않고, 확정 전 DS는 회수된다.
//      실제 D1Server.exe를 띄워 PID 생사로 판정. MATCH_DS_ENABLED=false거나 exe가 없으면 skip.
//   B. roster 재시작 생존 — 명단을 DB에 남긴 뒤 '별도 프로세스'가 loadActive로 복원해
//      그 매치의 결과 POST를 200으로 받는지. 재시작을 흉내내는 게 아니라 실제로 새 프로세스에서 확인한다.
//
// B의 자식 프로세스는 이 파일을 --phase=restarted 로 다시 실행한 것이다(같은 코드, 빈 메모리).
import type { ResultSetHeader, RowDataPacket } from 'mysql2';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { existsSync } from 'node:fs';
import type { AddressInfo } from 'node:net';

import buildApp from '../src/app.js';
import { config } from '../src/common/config.js';
import { closePool, getPool } from '../src/common/db.js';
import * as ds from '../src/match/ds.js';
import * as roster from '../src/match/roster.js';

const MAP = 'default map';

/** 자식(재시작된 백엔드) 역할로 실행됐는지. */
const restartedArg = process.argv.find((a) => a.startsWith('--phase=restarted'));

if (restartedArg)
{
    await runRestartedPhase();
}
else
{
    await runMain();
}

/**
 * 부모 — A/B 두 검사를 순서대로 수행.
 * B는 roster를 DB에만 남기고(메모리 캐시는 이 프로세스와 함께 사라진다) 자식에게 넘긴다.
 */
async function runMain(): Promise<void>
{
    let failed = 0;

    failed += await checkDsLifetimeSeparation() ? 0 : 1;
    failed += await checkRosterSurvivesRestart() ? 0 : 1;

    await closePool();

    if (failed > 0)
    {
        console.error(`\n${failed}개 검사 실패`);
        process.exit(1);
    }
    console.log('\n2/2 passed — 백엔드가 죽어도 경기는 끝나고 결과가 남는다');
}

/** A. 확정된 DS는 살아남고 확정 전 DS는 회수되는가 (실제 프로세스). */
async function checkDsLifetimeSeparation(): Promise<boolean>
{
    console.log('\n[A] DS 수명 분리 — 확정 DS 생존 / 미확정 DS 회수');

    if (!config.match.ds.enabled || !existsSync(config.match.ds.exePath))
    {
        console.log('  · skip — MATCH_DS_ENABLED=false이거나 MATCH_DS_EXE 경로 없음');

        return true;
    }

    const liveMatchId = `rs-live-${randomBytes(3).toString('hex')}`;
    const doomedMatchId = `rs-doomed-${randomBytes(3).toString('hex')}`;
    // PID는 shutdownUncommitted 전에 확보해 둔다 — 그 뒤엔 running에서 빠져 조회되지 않는다.
    let livePid: number | undefined;
    let doomedPid: number | undefined;

    try
    {
        // 프로세스 수명만 보는 검사라 실제 매치(roster·ready·플레이어)는 세우지 않고 spawn만 한다.
        const livePort = (await ds.allocate(liveMatchId, 'tok-live', 1, [])).port;
        const doomedPort = (await ds.allocate(doomedMatchId, 'tok-doomed', 1, [])).port;
        livePid = ds.findPid(livePort);
        doomedPid = ds.findPid(doomedPort);
        assert.ok(livePid !== undefined && doomedPid !== undefined, 'DS PID를 얻지 못함');

        // 성사 확정 = 플레이어가 붙은 상태. 여기부터 이 DS는 백엔드 소유가 아니다.
        ds.commit(liveMatchId);

        // 백엔드 종료(정상 shutdown·크래시 emergencyShutdown 공통 경로)
        ds.shutdownUncommitted();

        // taskkill /T가 트리를 정리할 시간을 준다.
        await delay(3000);

        assert.equal(isPidAlive(livePid), true,
            `확정된 DS(pid=${livePid})가 백엔드 종료에 함께 죽음 — 폭발 반경 미차단`);

        assert.equal(isPidAlive(doomedPid), false,
            `미확정 DS(pid=${doomedPid})가 회수되지 않음 — 고아 발생`);

        console.log(`  ✓ 확정 DS(pid=${livePid}) 생존 · 미확정 DS(pid=${doomedPid}) 회수됨`);

        return true;
    }
    catch (err)
    {
        console.error(`  ✗ ${err instanceof Error ? err.message : String(err)}`);

        return false;
    }
    finally
    {
        // 검사 성패와 무관하게 이 하네스가 띄운 프로세스는 남기지 않는다.
        // 살려둔 DS는 running에서 이미 빠졌으므로 ds가 아니라 여기서 직접 종료한다.
        for (const pid of [livePid, doomedPid])
        {
            if (pid !== undefined && isPidAlive(pid))
            {
                killPid(pid);
            }
        }
    }
}

/** B. DB에 남은 roster를 새 프로세스가 복원해 결과 POST를 받아내는가. */
async function checkRosterSurvivesRestart(): Promise<boolean>
{
    console.log('\n[B] roster 재시작 생존 — 새 프로세스가 결과 POST를 200으로 수신');

    try
    {
        const suffix = randomBytes(3).toString('hex');
        const users = await seedUsers('rs', suffix, config.match.playersPerMatch);
        const matchId = `rs-${suffix}-${randomBytes(4).toString('hex')}`;
        const serverToken = randomBytes(24).toString('base64url');

        // 매치 성사 — 이 시점의 명단이 DB에 남는다.
        await roster.register({
            matchId,
            serverToken,
            mapName: MAP,
            startedAt: Date.now(),
            players: users.map((u, i) => ({ userId: u.userId, nickname: u.nickname, joinToken: `rsjoin${i}` })),
        });

        const persisted = await countRosterRows(matchId);
        assert.equal(persisted, 1, 'roster가 DB에 저장되지 않음 — 재시작하면 결과가 404가 된다');
        console.log('  · roster DB 저장 확인');

        // 여기서 이 프로세스의 메모리 캐시는 의미를 잃는다. 자식은 DB만 보고 시작한다.
        const scoreBefore = await selectScore(users.map((u) => u.userId));

        const payload = JSON.stringify({
            matchId,
            serverToken,
            body: {
                matchId,
                mapName: MAP,
                durationSec: 111,
                endReason: 'winner',
                results: users.map((u, i) => ({ userId: u.userId, slotIndex: i, placement: i + 1, livesLeft: i === 0 ? 1 : 0 })),
            },
        });

        // execArgv를 그대로 물려줘야 자식도 tsx 로더로 이 .ts를 실행한다.
        const child = spawnSync(process.execPath, [...process.execArgv, process.argv[1], '--phase=restarted', payload],
            { encoding: 'utf8', env: process.env });
        const childOut = `${child.stdout ?? ''}${child.stderr ?? ''}`.trim();
        assert.equal(child.status, 0, `재시작된 백엔드에서 결과 수신 실패:\n${indent(childOut)}`);
        console.log(`  · ${childOut.split('\n').filter((l) => l.startsWith('[restarted]')).join(' / ')}`);

        // 결과가 실제로 반영됐는지는 부모가 DB로 직접 확인한다(자식 자기보고 신뢰 금지).
        const scoreAfter = await selectScore(users.map((u) => u.userId));
        const winner = users[0].userId;
        assert.notEqual(scoreAfter.get(winner), scoreBefore.get(winner),
            '결과는 200이었지만 player_profiles.score가 그대로 — 저장 경로가 끊김');

        console.log(`  ✓ 1위 score ${scoreBefore.get(winner)!} → ${scoreAfter.get(winner)!}`);

        return true;
    }
    catch (err)
    {
        console.error(`  ✗ ${err instanceof Error ? err.message : String(err)}`);

        return false;
    }
}

/**
 * 자식 — "재시작된 백엔드" 역할. 메모리 roster는 비어 있는 상태로 시작한다.
 * index.ts와 같은 순서로 loadActive를 거친 뒤 결과 POST를 받는다.
 */
async function runRestartedPhase(): Promise<void>
{
    const { matchId, serverToken, body } = JSON.parse(process.argv[3]) as
        { matchId: string; serverToken: string; body: unknown };

    // 복원 전에는 이 매치를 몰라야 정상 — 복원의 효과를 증명하는 전제다.
    assert.equal(roster.get(matchId), undefined, '새 프로세스인데 roster가 이미 메모리에 있음');

    const restored = await roster.loadActive();
    console.log(`[restarted] roster 복원 ${restored}건`);
    assert.ok(roster.get(matchId) !== undefined, `복원됐지만 대상 매치(${matchId})가 없음`);

    const app = buildApp();
    const server = app.listen(0);
    await new Promise<void>((resolve) => server.once('listening', () => resolve()));
    const { port } = server.address() as AddressInfo;

    const res = await fetch(`http://127.0.0.1:${port}/api/match/result`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${serverToken}` },
        body: JSON.stringify(body),
    });
    const json = await res.json() as { ok: boolean; error?: { code: string } };
    console.log(`[restarted] 결과 POST → ${res.status} ${json.error?.code ?? 'ok'}`);

    server.close();
    await closePool();

    // 404 MATCH_NOT_FOUND가 정확히 이 작업 전의 증상이다.
    assert.equal(res.status, 200, `결과 POST가 ${res.status}(${json.error?.code ?? '-'}) — 재시작 후 결과 유실`);
}

/** HTTP register(rate-limit) 우회 — users+player_profiles를 DB에 직접 넣는다. */
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
        await getPool().execute('INSERT INTO player_profiles (user_id) VALUES (?)', [ins.insertId]);
        out.push({ userId: ins.insertId, nickname });
    }

    return out;
}

async function countRosterRows(matchId: string): Promise<number>
{
    const [rows] = await getPool().query<RowDataPacket[]>(
        'SELECT COUNT(*) AS n FROM match_rosters WHERE match_id = ?', [matchId]);

    return Number(rows[0].n);
}

async function selectScore(userIds: number[]): Promise<Map<number, number>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await getPool().query<RowDataPacket[]>(
        `SELECT user_id, score FROM player_profiles WHERE user_id IN (${placeholders})`, userIds);

    return new Map(rows.map((r) => [Number(r.user_id), Number(r.score)]));
}

/** Windows에서 PID 생존 확인 — signal 0은 프로세스를 건드리지 않고 존재 여부만 본다. */
function isPidAlive(pid: number): boolean
{
    try
    {
        process.kill(pid, 0);

        return true;
    }
    catch
    {
        return false;
    }
}

/** 하네스 뒷정리 — 런처가 실제 서버를 자식으로 띄우므로 트리째 종료. */
function killPid(pid: number): void
{
    spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { stdio: 'ignore' });
}

function delay(ms: number): Promise<void>
{
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function indent(text: string): string
{
    return text.split('\n').map((l) => `      ${l}`).join('\n');
}
