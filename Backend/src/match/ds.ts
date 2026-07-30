// 매치당 Dedicated Server 프로세스 할당자. config.match.ds로 제어.
// 매치 성사 시 D1Server.exe를 빈 포트로 spawn 후 즉시 주소 반환(준비 완료는 DS의 ready 콜백으로 대기). 종료 시 전부 kill.
// Windows 전용: 루트 D1Server.exe가 실제 서버를 자식으로 spawn하므로 kill은 taskkill /T(트리)로 한다.
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import dgram from 'node:dgram';

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
// 포트 프로브가 비동기라 "프로브 통과 → running.set" 사이에 다른 allocate가 끼어들 수 있다.
// runMatchCycle이 handleMatch를 동시에 여러 개 띄우므로(ws.ts) 후보 포트를 동기적으로 선점해 둔다.
const reserved = new Set<number>();

/** 빈 포트에 DS spawn → 즉시 {host, port}. 준비 완료는 호출측이 readiness로 대기. 포트 고갈 시 throw. */
export async function allocate(matchId: string, serverToken: string, expectedPlayers: number, roster: { joinToken: string; userId: number; nickname: string }[], bots: { userId: number; nickname: string }[] = []): Promise<{ host: string; port: number }>
{
    const port = await reserveFreePort();
    if (port === null)
    {
        throw new Error(`DS 포트 풀 고갈 (${ds.portMin}-${ds.portMax}, 가동 ${running.size}개, 외부 점유 포함)`);
    }

    try
    {
        spawnOnPort(port, matchId, serverToken, expectedPlayers, roster, bots);
    }
    finally
    {
        // running에 등재됐든 spawn이 실패했든, 선점은 여기서 해제한다(예약 누수 = 풀 축소).
        reserved.delete(port);
    }

    return { host: ds.host, port };
}

/** 선점된 포트에 실제 DS 프로세스를 띄우고 running에 등재. allocate 전용. */
function spawnOnPort(port: number, matchId: string, serverToken: string, expectedPlayers: number, roster: { joinToken: string; userId: number; nickname: string }[], bots: { userId: number; nickname: string }[]): void
{
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

/**
 * 부팅 시 포트 풀 점검 — 이전 실행이 크래시로 남긴 고아 DS를 로그로 드러낸다.
 * 죽이지는 않는다: exe 이름 기준 일괄 taskkill은 개발자가 디버깅용으로 직접 띄운 DS까지 죽인다.
 * 점유된 포트는 reserveFreePort의 프로브가 매번 걸러내므로, 여기서는 원인 추적용 기록만 남기면 충분하다.
 */
export async function reapOrphans(): Promise<void>
{
    const occupied: number[] = [];
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (!(await isPortAvailable(p)))
        {
            occupied.push(p);
        }
    }

    if (occupied.length > 0)
    {
        logger.warn({ occupied, portMin: ds.portMin, portMax: ds.portMax },
            'DS 포트 점유 감지 — 이전 실행의 고아 프로세스일 수 있음. 해당 포트는 할당에서 제외된다');
    }
}

/**
 * 빈 포트 하나를 선점해 반환. 없으면 null.
 * 3중 확인 — running(내가 쓰는 중) + reserved(다른 allocate가 선점 중) + UDP 프로브(외부 점유).
 * 프로브가 필요한 이유: running Map만 믿으면 이전 실행의 고아 DS가 쥔 포트를 재배정하고,
 * 새 DS는 bind에 실패해 증상이 30초 뒤 readiness 타임아웃으로만 나타난다(원인 추적 불가).
 */
async function reserveFreePort(): Promise<number | null>
{
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (running.has(p) || reserved.has(p))
        {
            continue;
        }

        // await 앞에서 동기 선점 — 프로브 대기 중 다른 allocate가 같은 포트를 집어가는 것을 막는다.
        reserved.add(p);
        if (await isPortAvailable(p))
        {
            return p;
        }

        reserved.delete(p);
        logger.warn({ port: p }, 'DS 포트가 외부 프로세스에 점유됨 — 건너뜀');
    }

    return null;
}

/**
 * 포트 사용 가능 여부를 UDP로 프로브.
 * UE의 -port는 IpNetDriver 게임 트래픽 포트라 UDP다 — TCP(net.createServer)로 확인하면
 * DS가 점유 중인 포트도 "비었다"고 나와 프로브가 무의미해진다.
 * dgram의 reuseAddr 기본값이 false라 점유된 포트는 EADDRINUSE로 error 이벤트가 뜬다.
 * 주소를 지정하지 않아 와일드카드(0.0.0.0)로 바인드 — DS가 어느 인터페이스에 붙었든 검출된다.
 */
function isPortAvailable(port: number): Promise<boolean>
{
    return new Promise((resolve) =>
    {
        const probe = dgram.createSocket('udp4');
        probe.once('error', () =>
        {
            resolve(false);
        });

        probe.once('listening', () =>
        {
            probe.close(() =>
            {
                resolve(true);
            });
        });
        probe.bind(port);
    });
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
        // error 리스너 필수 — 리스너 없는 ChildProcess의 'error'는 throw된다(PATH 문제·권한·프로세스 한계).
        // 잡지 않으면 DS를 정리하려던 코드가 백엔드를 내려 오히려 남은 DS 전부를 고아로 만든다.
        const killer = spawn('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true });
        killer.on('error', (err) =>
        {
            logger.error({ err, pid, port }, 'taskkill spawn 실패 — DS가 고아로 남을 수 있음');
        });
    }
}
