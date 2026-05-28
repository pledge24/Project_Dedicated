// D1 백엔드 엔트리: env 로드 → 검증 → app 구성 → listen → graceful shutdown
import 'dotenv/config';

import buildApp from './app.js';
import { config } from './common/config.js';
import { closePool } from './common/db.js';

const app = buildApp();

const server = app.listen(config.port, () => {
    console.log(`[D1 Backend] listening on http://127.0.0.1:${config.port}`);
});

const SHUTDOWN_TIMEOUT_MS = 10_000;

let shuttingDown = false;

async function shutdown(signal)
{
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(`[D1 Backend] ${signal} 수신 — graceful shutdown 시작`);

    // 안전망: close가 응답 안 하면 강제 종료
    const force = setTimeout(() => {
        console.error('[D1 Backend] shutdown 타임아웃 — 강제 종료');
        process.exit(1);
    }, SHUTDOWN_TIMEOUT_MS);
    force.unref();

    server.close(async (err) => {
        if (err)
        {
            console.error('[D1 Backend] HTTP 서버 close 실패:', err);
            process.exit(1);
            return;
        }
        try
        {
            await closePool();
            console.log('[D1 Backend] graceful shutdown 완료');
            process.exit(0);
        }
        catch (poolErr)
        {
            console.error('[D1 Backend] DB pool close 실패:', poolErr);
            process.exit(1);
        }
    });
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT',  () => shutdown('SIGINT'));
