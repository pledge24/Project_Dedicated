// 매치당 Dedicated Server 프로세스 할당자. config.match.ds로 제어.
// 매치 성사 시 D1Server.exe를 빈 포트로 spawn 후 즉시 주소 반환(준비 완료는 DS의 ready 콜백으로 대기).
// DS 수명은 commit 시점부터 백엔드 수명과 분리된다 — 종료 시 회수 대상은 확정 전 DS뿐(shutdownUncommitted).
// Windows 전용: 루트 D1Server.exe가 실제 서버를 자식으로 spawn하므로 kill은 taskkill /T(트리)로 한다.
import { spawn } from 'node:child_process';
import type { ChildProcess } from 'node:child_process';
import dgram from 'node:dgram';
import { existsSync, unlinkSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import { config } from '../common/config.js';
import { logger } from '../common/logger.js';
import * as readiness from './readiness.js';

interface DsProcess
{
    child: ChildProcess;
    matchId: string;
    port: number;
    killTimer: NodeJS.Timeout;
    /** 매치 성사가 확정돼 플레이어가 붙은 서버인가. true면 백엔드 종료가 이 프로세스를 죽이지 않는다. */
    isCommitted: boolean;
}

/** DS에 넘길 매치 설정. 커맨드라인에 두면 안 되는 값(토큰)이 전부 여기 모인다. */
interface MatchConfigFile
{
    matchId: string;
    serverToken: string;
    expectedPlayers: number;
    roster: { joinToken: string; userId: number; nickname: string }[];
    bots: { userId: number; nickname: string }[];
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

/**
 * 매치 성사 확정 — 이 DS는 백엔드가 소유한 자원이 아니라 독립 워크로드가 된다.
 * 이후 백엔드가 어떻게 죽든(정상 종료·크래시) 이 프로세스는 건드리지 않는다.
 * 4명이 플레이 중인 게임 서버를 파일 디스크립터처럼 "종료 시 정리할 자원"으로 취급하면
 * 무관한 코드의 예외 하나가 진행 중인 경기 전부를 끝내버린다.
 */
export function commit(matchId: string): void
{
    for (const proc of running.values())
    {
        if (proc.matchId === matchId)
        {
            proc.isCommitted = true;

            return;
        }
    }

    // 이미 종료된 DS 등. 확정 자체는 진행돼야 하므로 throw하지 않고 기록만 한다.
    logger.warn({ matchId }, 'DS 확정 대상 없음 — 이미 종료됐거나 프로세스 미보유');
}

/**
 * 프로세스 종료 시 정리 — 확정 전 DS만 회수하고 라이브 매치는 살려 둔다.
 * 살려둔 DS는 자체 로직으로 종료된다(매치 시간 + 종료 grace → RequestExit).
 * 남은 포트는 다음 기동의 reapOrphans/UDP 프로브가 점유로 감지해 할당에서 제외하므로,
 * "죽이지 않고 드러낸다"는 기존 고아 처리 방침과 일관된다.
 */
export function shutdownUncommitted(): void
{
    let reaped = 0;
    let live = 0;

    for (const proc of [...running.values()])
    {
        if (proc.isCommitted)
        {
            // kill하지 않고 핸들만 놓아준다(killTimer는 이 프로세스와 함께 사라진다).
            clearTimeout(proc.killTimer);
            running.delete(proc.port);
            live += 1;
            continue;
        }

        killProcess(proc.port);
        reaped += 1;
    }

    if (reaped || live)
    {
        logger.info({ reaped, live }, '확정 전 DS 회수 — 라이브 DS는 자체 수명으로 종료됨');
    }
}

/** 해당 매치의 DS를 즉시 회수. 확정 창에서 매치가 취소돼 스폰한 DS를 버릴 때 사용. */
export function release(matchId: string): void
{
    for (const proc of running.values())
    {
        if (proc.matchId === matchId)
        {
            killProcess(proc.port);

            return;
        }
    }
}

/**
 * 진단용 — 해당 포트를 쥔 DS의 PID. 보유하지 않으면 undefined.
 * 고아 추적(어느 프로세스가 포트를 잡고 있나)과 수명 분리 검증에 쓴다.
 * 확정 후 shutdownUncommitted를 지난 DS는 running에서 빠지므로 여기서도 조회되지 않는다.
 */
export function findPid(port: number): number | undefined
{
    return running.get(port)?.child.pid;
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

/** 선점된 포트에 실제 DS 프로세스를 띄우고 running에 등재. allocate 전용. */
function spawnOnPort(port: number, matchId: string, serverToken: string, expectedPlayers: number, roster: { joinToken: string; userId: number; nickname: string }[], bots: { userId: number; nickname: string }[]): void
{
    /**
     * 매치 설정은 파일로 넘기고 커맨드라인에는 경로만 둔다.
     * 커맨드라인은 같은 사용자 세션의 아무 프로세스나 읽을 수 있는데(작업관리자·Get-Process·WMI),
     * 거기 실려 있던 serverToken은 결과 위조 권한이고 roster의 joinToken은 남의 신원으로 입장할 권한이다.
     * 서버 권위 모델 전체가 이 두 값에 걸려 있으므로 노출 창을 "매치 내내"에서 "DS가 읽을 때까지"로 줄인다.
     * DS는 읽는 즉시 파일을 지우고, 백엔드도 안전망 타이머로 한 번 더 지운다(DS가 못 뜬 경우).
     *
     * expectedPlayers: DS가 전원 입장까지 매치 시작을 미루는 게이트용(미충족 시 DS 측 타임아웃으로 시작).
     *                  총원(휴먼+봇)이라 봇이 PlayerArray를 채우고 휴먼 입장 시 게이트가 충족된다.
     * roster:          DS가 ?join= 토큰으로 권위 신원(userId·이름)을 매핑. 좌석은 DS가 랜덤 배정.
     * bots:            봇전(Bot-Fill) 봇 좌석. 토큰 없음(DS가 서버측 스폰). userId는 음수 sentinel.
     * stdio 'ignore' — DS는 -log로 자체 콘솔/로그파일에 기록. 파이프 미소비로 막히는 것 방지.
     */
    const configPath = writeMatchConfig(matchId, {
        matchId,
        serverToken,
        expectedPlayers,
        roster: roster.map((r) => ({ joinToken: r.joinToken, userId: r.userId, nickname: r.nickname })),
        bots: bots.map((b) => ({ userId: b.userId, nickname: b.nickname })),
    });

    // BackendUrl은 비밀이 아니다 — 토큰을 파일로 뺀 위 근거(노출 시 결과 위조·신원 도용)가
    // 주소에는 적용되지 않으므로 커맨드라인에 둔다. 넘기지 않으면 DS가 컴파일 시점 기본값을 쓴다.
    const args = [ds.map, `-port=${port}`, `-MatchConfig=${configPath}`, `-BackendUrl=${ds.backendUrl}`, '-log'];
    const child = spawn(ds.exePath, args, { stdio: 'ignore', windowsHide: false });
    scheduleConfigCleanup(configPath, matchId);

    const killTimer = setTimeout(() =>
    {
        logger.warn({ port, matchId }, 'DS 최대 수명 초과 — 회수');
        killProcess(port);
    }, ds.maxLifetimeMs);
    if (typeof killTimer.unref === 'function')
    {
        killTimer.unref();
    }

    running.set(port, { child, matchId, port, killTimer, isCommitted: false });

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

/**
 * 설정 파일을 임시 디렉터리에 쓰고 경로 반환.
 * mode 0o600 — POSIX에서 소유자 외 읽기 차단. Windows는 mode를 무시하지만 %TEMP%가 이미 사용자별이다.
 * 실패는 throw해 allocate가 매치를 버리게 한다(토큰 없이 뜬 DS는 결과를 보고할 수 없다).
 */
function writeMatchConfig(matchId: string, cfg: MatchConfigFile): string
{
    const filePath = path.join(os.tmpdir(), `d1-match-${matchId}.json`);
    writeFileSync(filePath, JSON.stringify(cfg), { encoding: 'utf8', mode: 0o600 });

    return filePath;
}

/**
 * 안전망 삭제 — 정상 경로에서는 DS가 읽자마자 지운다. DS가 못 뜨거나 크래시한 경우를 대비해
 * 백엔드도 한 번 더 지운다. 준비 타임아웃(readyTimeoutMs)보다 넉넉히 뒤에 돌아 정상 부팅을 방해하지 않는다.
 */
function scheduleConfigCleanup(filePath: string, matchId: string): void
{
    const timer = setTimeout(() =>
    {
        try
        {
            if (existsSync(filePath))
            {
                unlinkSync(filePath);
                logger.warn({ matchId, filePath }, 'DS가 매치 설정 파일을 지우지 않음 — 백엔드가 정리');
            }
        }
        catch (err)
        {
            logger.warn({ err, matchId, filePath }, '매치 설정 파일 정리 실패');
        }
    }, ds.readyTimeoutMs * 2);
    timer.unref();
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
