// 매치당 Dedicated Server 프로세스 할당자. config.match.ds로 제어.
// 매치 성사 시 D1Server.exe를 빈 포트로 spawn 후 즉시 주소 반환(준비 완료는 DS의 ready 콜백으로 대기). 종료 시 전부 kill.
// Windows 전용: 루트 D1Server.exe가 실제 서버를 자식으로 spawn하므로 kill은 taskkill /T(트리)로 한다.
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';

import { config } from '../common/config.js';
import { logger } from '../common/logger.js';
import * as readiness from './readiness.js';

interface DsProcess
{
    child: ChildProcess;
    matchId: string;
    port: number;
    killTimer: NodeJS.Timeout;
}

const ds = config.match.ds;
const running = new Map<number, DsProcess>(); // port → 프로세스

/** 빈 포트에 DS spawn → 즉시 {host, port}. 준비 완료는 호출측이 readiness로 대기. 포트 고갈 시 throw. */
export function allocate(matchId: string, serverToken: string, expectedPlayers: number, roster: { joinToken: string; userId: number; nickname: string }[], bots: { userId: number; nickname: string }[] = []): { host: string; port: number }
{
    const port = pickFreePort();
    if (port === null)
    {
        throw new Error(`DS 포트 풀 고갈 (${ds.portMin}-${ds.portMax}, 가동 ${running.size}개)`);
    }

    /**
     * matchId·serverToken·roster는 커맨드라인 스위치로 주입하면, DS가 FParse로 읽는다.
     * expectedPlayers: DS가 전원 입장까지 매치 시작을 미루는 게이트용(미충족 시 DS 측 타임아웃으로 시작).
     * Roster:          token:userId:base64(nickname);… — DS가 ?join= 토큰으로 권위 신원(userId·이름)을 매핑. 좌석은 DS가 랜덤 배정.
     *                   닉네임은 한글(비-ASCII)이라 Windows 커맨드라인 코드페이지 깨짐 방지 위해 표준 base64로 인코딩(UE FBase64::Decode 호환).
     * Bots:            userId:base64(nickname);… — 봇전(Bot-Fill) 봇 좌석. 토큰 없음(DS가 서버측 스폰). userId는 음수 sentinel.
     *                   ExpectedPlayers는 총원(휴먼+봇)이라 봇이 PlayerArray를 채우고 휴먼 입장 시 시작 게이트가 충족된다.
     * stdio 'ignore' — DS는 -log로 자체 콘솔/로그파일에 기록. 파이프 미소비로 막히는 것 방지.
     */
    const rosterArg = roster.map((r) => `${r.joinToken}:${r.userId}:${Buffer.from(r.nickname, 'utf8').toString('base64')}`).join(';');
    const args = [ds.map, `-port=${port}`, `-MatchId=${matchId}`, `-MatchToken=${serverToken}`, `-ExpectedPlayers=${expectedPlayers}`, `-Roster=${rosterArg}`];
    if (bots.length > 0)
    {
        const botsArg = bots.map((b) => `${b.userId}:${Buffer.from(b.nickname, 'utf8').toString('base64')}`).join(';');
        args.push(`-Bots=${botsArg}`);
    }
    args.push('-log');
    const child = spawn(ds.exePath, args, { stdio: 'ignore', windowsHide: false });

    const killTimer = setTimeout(() =>
    {
        logger.warn({ port, matchId }, 'DS 최대 수명 초과 — 회수');
        killProcess(port);
    }, ds.maxLifetimeMs);
    if (typeof killTimer.unref === 'function')
    {
        killTimer.unref();
    }

    running.set(port, { child, matchId, port, killTimer });

    child.on('exit', (code) =>
    {
        const cur = running.get(port);
        if (cur && cur.child === child)
        {
            clearTimeout(cur.killTimer);
            running.delete(port);
        }
        // 준비 콜백 전에 죽었으면(부팅 중 크래시) 대기 중인 매치를 즉시 실패시켜 타임아웃까지 안 기다림.
        // 확정/정상종료된 매치는 gate가 이미 소비돼 no-op.
        readiness.fail(matchId, `DS 프로세스 종료(code=${code}, port=${port})`);
        logger.info({ port, matchId, code }, 'DS 프로세스 종료');
    });

    child.on('error', (err) =>
    {
        logger.error({ err, port, matchId, exePath: ds.exePath }, 'DS spawn 실패');
        readiness.fail(matchId, `DS spawn 실패(port=${port})`);
        killProcess(port);
    });

    logger.info({ port, matchId, map: ds.map }, 'DS spawn — 준비 콜백 대기');

    return { host: ds.host, port };
}

/** graceful shutdown — 가동 중인 DS 전부 종료. */
export function shutdownAll(): void
{
    const ports = [...running.keys()];
    for (const port of ports)
    {
        killProcess(port);
    }

    if (ports.length)
    {
        logger.info({ count: ports.length }, 'DS 전부 종료');
    }
}

export function runningCount(): number
{
    return running.size;
}

/** 특정 포트의 DS를 즉시 회수. 확정 창에서 매치가 취소돼 스폰한 DS를 버릴 때 사용(killProcess public 래퍼). */
export function release(port: number): void
{
    killProcess(port);
}

function pickFreePort(): number | null
{
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (!running.has(p))
        {
            return p;
        }
    }

    return null;
}

/** 포트의 DS를 트리째 종료하고 풀에서 해제. */
function killProcess(port: number): void
{
    const proc = running.get(port);
    if (!proc)
    {
        return;
    }

    clearTimeout(proc.killTimer);
    running.delete(port);

    const pid = proc.child.pid;
    if (pid !== undefined)
    {
        // 런처가 실제 서버를 자식으로 띄우므로 /T로 트리 전체 종료.
        spawn('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true });
    }
}
