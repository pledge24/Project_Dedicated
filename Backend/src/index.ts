// D1 백엔드 엔트리: env 로드 → 검증 → app 구성 → listen → graceful shutdown
import 'dotenv/config';

import buildApp from './app.js';
import { config } from './common/config.js';
import { closePool } from './common/db.js';
import { logger } from './common/logger.js';

const app = buildApp();

const server = app.listen(config.port, () =>
{
    logger.info({ port: config.port }, 'D1 Backend 리스닝 시작');
});

const SHUTDOWN_TIMEOUT_MS = 10_000;

let shuttingDown = false;

async function shutdown(signal: string): Promise<void>
{
    if (shuttingDown) return;
    shuttingDown = true;
    logger.info({ signal }, 'graceful shutdown 시작');

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

process.on('SIGTERM', () => { void shutdown('SIGTERM'); });
process.on('SIGINT',  () => { void shutdown('SIGINT'); });
