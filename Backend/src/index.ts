// D1 백엔드 엔트리: env 로드 → 검증 → app 구성 → WS 부착 → listen → graceful shutdown
import 'dotenv/config';

import http from 'node:http';

import buildApp from './app.js';
import { config } from './common/config.js';
import { closePool } from './common/db.js';
import { logger } from './common/logger.js';
import { allocator } from './match/dsAllocator.js';
import * as roster from './match/roster.js';
import { attachMatchWebSocket } from './match/ws.js';

const app = buildApp();

// ws가 같은 http.Server를 공유해야 해서 명시적으로 생성(app.listen 대신).
const server = http.createServer(app);
const matchWs = attachMatchWebSocket(server);

// 진행 중이던 매치 명단 복원 — 지난 실행에서 살아남은 DS가 결과를 보고할 때 serverToken 검증에 필요하다.
// reapOrphans와 달리 실패 시 기동을 막는다: roster 없이 뜨면 그 DS들의 결과가 전부 404로 버려지고,
// 증상은 "점수가 안 올랐다"로만 나타나 원인 추적이 사실상 불가능해진다.
const restoredRosters = await roster.loadActive();
if (restoredRosters > 0)
{
    logger.info({ count: restoredRosters }, '진행 중이던 매치 roster 복원 — 결과 보고 수신 가능');
}

// 리스닝 전에 DS 잔재 점검 — 이전 실행이 남긴 DS(크래시 고아 + 살려 보낸 라이브 매치)를 로그로 드러낸다(stub은 no-op).
// 실패해도 기동은 막지 않는다(진단용이라 서비스 가용성보다 우선순위가 낮다).
try
{
    await allocator.reapOrphans();
}
catch (err)
{
    logger.warn({ err }, 'DS 잔재 점검 실패 — 건너뜀');
}

server.listen(config.port, () =>
{
    logger.info({ port: config.port }, 'D1 Backend 리스닝 시작');
});

const SHUTDOWN_TIMEOUT_MS = 10_000;

let shuttingDown = false;

async function shutdown(signal: string): Promise<void>
{
    if (shuttingDown)
    {
        return;
    }
    shuttingDown = true;
    logger.info({ signal }, 'graceful shutdown 시작');

    // 매칭 사이클/heartbeat 정지 + WS 클라 종료(열린 소켓이 server.close를 막는 걸 방지).
    matchWs.stop();

    // 안전망: close가 응답 안 하면 강제 종료
    const force = setTimeout(() =>
    {
        logger.error('shutdown 타임아웃 — 강제 종료');
        process.exit(1);
    }, SHUTDOWN_TIMEOUT_MS);
    force.unref();

    server.close(async (err) =>
    {
        if (err)
        {
            logger.error({ err }, 'HTTP 서버 close 실패');
            process.exit(1);

            return;
        }
        try
        {
            await closePool();
            logger.info('graceful shutdown 완료');
            process.exit(0);
        }
        catch (poolErr)
        {
            logger.error({ err: poolErr }, 'DB pool close 실패');
            process.exit(1);
        }
    });
}

/**
 * 크래시 경로 전용 정리 — 동기 작업만 하고 즉시 exit(1).
 * shutdown()을 재사용하지 않는 이유: 그건 server.close 콜백에서 await closePool() 후 exit(0)하는
 * 정상 종료 경로다. Node 문서는 uncaughtException 시점을 "정의되지 않은 상태"로 규정하고
 * "할당된 리소스의 동기 정리만 하고 종료"를 권고하므로, 여기서 async를 기다리면 안 된다.
 * 단 "할당된 리소스"에 라이브 DS는 포함되지 않는다 — 플레이 중인 게임 서버는 이 프로세스의
 * 자원이 아니라 독립 워크로드다. matchWs.stop() → ds.shutdownUncommitted()가 확정 전 DS만 회수하고,
 * 진행 중인 경기는 살아남아 재시작한 백엔드에 결과를 보고한다(roster가 DB에 있으므로 인증된다).
 * killProcess()는 taskkill을 spawn만 하고 기다리지 않지만, Windows 자식 프로세스는
 * 부모가 exit해도 살아남으므로 taskkill은 완주한다.
 */
function emergencyShutdown(reason: string, err: unknown): void
{
    if (shuttingDown)
    {
        return;
    }
    shuttingDown = true;

    // dev는 pino-pretty가 워커 스레드 transport라 exit 직전 로그가 유실될 수 있다.
    // 고아 DS를 추적할 단서는 반드시 남아야 하므로 stderr에 동기로 한 줄 먼저 박는다.
    process.stderr.write(`[FATAL] ${reason} — DS 정리 후 즉시 종료\n${String(err instanceof Error ? err.stack : err)}\n`);
    logger.fatal({ err, reason }, '치명적 오류 — DS 정리 후 즉시 종료');

    try
    {
        matchWs.stop();
    }
    catch (stopErr)
    {
        process.stderr.write(`[FATAL] DS 정리 실패: ${String(stopErr)}\n`);
    }

    process.exit(1);
}

process.on('SIGTERM', () =>
{
    void shutdown('SIGTERM');
});

process.on('SIGINT',  () =>
{
    void shutdown('SIGINT');
});

// SIGHUP: Windows에서 콘솔 창을 닫을 때 발생. 단 Windows는 약 10초 후 무조건 종료시키므로
// SHUTDOWN_TIMEOUT_MS(10s)와 경합한다 — 그래도 정상 경로를 타는 게 DS 정리 확률이 높다.
process.on('SIGHUP', () =>
{
    void shutdown('SIGHUP');
});

// SIGBREAK: Windows Ctrl+Break. 비Windows에서는 발생하지 않지만 등록 자체는 안전(Node 문서).
process.on('SIGBREAK', () =>
{
    void shutdown('SIGBREAK');
});

// 여기까지 오면 앱 상태는 신뢰할 수 없다. DS만 확실히 정리하고 죽는다.
process.on('uncaughtException', (err) =>
{
    emergencyShutdown('uncaughtException', err);
});

// 미처리 rejection은 Node에서 uncaughtException으로 승격되지만, 승격 전에 잡아 DS를 정리한다.
process.on('unhandledRejection', (reason) =>
{
    emergencyShutdown('unhandledRejection', reason);
});
