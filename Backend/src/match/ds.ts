// 매치당 Dedicated Server 프로세스 할당자. config.match.ds로 제어.
// 매치 성사 시 D1Server.exe를 빈 포트로 spawn → 고정 부팅 지연 후 주소 반환. 종료 시 전부 kill.
// Windows 전용: 루트 D1Server.exe가 실제 서버를 자식으로 spawn하므로 kill은 taskkill /T(트리)로 한다.
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';

import { config } from '../common/config.js';
import { logger } from '../common/logger.js';

const ds = config.match.ds;

interface DsProcess
{
    child: ChildProcess;
    matchId: string;
    port: number;
    killTimer: NodeJS.Timeout;
}

const running = new Map<number, DsProcess>(); // port → 프로세스

function delay(ms: number): Promise<void>
{
    return new Promise((resolve) => { setTimeout(resolve, ms); });
}

function pickFreePort(): number | null
{
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (!running.has(p)) return p;
    }
    return null;
}

/** 포트의 DS를 트리째 종료하고 풀에서 해제. */
function killProcess(port: number): void
{
    const proc = running.get(port);
    if (!proc) return;

    clearTimeout(proc.killTimer);
    running.delete(port);

    const pid = proc.child.pid;
    if (pid !== undefined)
    {
        // 런처가 실제 서버를 자식으로 띄우므로 /T로 트리 전체 종료.
        spawn('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true });
    }
}

/** 빈 포트에 DS spawn → bootDelay 후 {host, port}. 포트 고갈/부팅 실패 시 throw. */
export async function allocate(matchId: string): Promise<{ host: string; port: number }>
{
    const port = pickFreePort();
    if (port === null)
    {
        throw new Error(`DS 포트 풀 고갈 (${ds.portMin}-${ds.portMax}, 가동 ${running.size}개)`);
    }

    // stdio 'ignore' — DS는 -log로 자체 콘솔/로그파일에 기록. 파이프 미소비로 막히는 것 방지.
    const child = spawn(ds.exePath, [ds.map, `-port=${port}`, '-log'], { stdio: 'ignore', windowsHide: false });

    const killTimer = setTimeout(() =>
    {
        logger.warn({ port, matchId }, 'DS 최대 수명 초과 — 회수');
        killProcess(port);
    }, ds.maxLifetimeMs);
    if (typeof killTimer.unref === 'function') killTimer.unref();

    running.set(port, { child, matchId, port, killTimer });

    child.on('exit', (code) =>
    {
        const cur = running.get(port);
        if (cur && cur.child === child)
        {
            clearTimeout(cur.killTimer);
            running.delete(port);
        }
        logger.info({ port, matchId, code }, 'DS 프로세스 종료');
    });
    child.on('error', (err) =>
    {
        logger.error({ err, port, matchId, exePath: ds.exePath }, 'DS spawn 실패');
        killProcess(port);
    });

    logger.info({ port, matchId, map: ds.map }, 'DS spawn — 부팅 대기');
    await delay(ds.bootDelayMs);

    // 부팅 대기 중 죽었으면(spawn 실패/즉시 크래시) 실패 처리.
    if (!running.has(port))
    {
        throw new Error(`DS가 부팅 중 종료됨 (port=${port})`);
    }

    return { host: ds.host, port };
}

/** graceful shutdown — 가동 중인 DS 전부 종료. */
export function shutdownAll(): void
{
    const ports = [...running.keys()];
    for (const port of ports) killProcess(port);
    if (ports.length) logger.info({ count: ports.length }, 'DS 전부 종료');
}

export function runningCount(): number
{
    return running.size;
}
