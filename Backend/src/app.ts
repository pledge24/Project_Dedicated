// Express 앱 조립: 미들웨어 → 라우터 → 에러 핸들러
import { randomUUID } from 'node:crypto';

import express from 'express';
import type { Request, Response, NextFunction, Express } from 'express';
import { pinoHttp } from 'pino-http';

import { ok, fail } from './common/envelope.js';
import { AppError, Codes } from './common/errors.js';
import { logger } from './common/logger.js';
import { pingDb } from './common/db.js';
import authRouter from './auth/auth.router.js';

export default function buildApp(): Express
{
    const app = express();

    /*--------------
        Middleware
    --------------*/

    // 요청 로깅 + 상관 ID. 가장 앞에 두어 req.log/req.id가 어디서나 존재하게 함.
    app.use(pinoHttp({
        logger,
        genReqId(req, res)
        {
            const incoming = req.headers['x-request-id'];
            const id = (typeof incoming === 'string' && incoming) ? incoming : randomUUID();
            res.setHeader('X-Request-Id', id);
            return id;
        },
        customLogLevel(_req, res, err)
        {
            if (res.statusCode >= 500 || err) return 'error';
            if (res.statusCode >= 400) return 'warn';
            return 'info';
        },
    }));
    
    app.use(express.json({ limit: '32kb' }));

    /*--------------
          Router
    --------------*/

    // 사람용 루트 (프로브는 /healthz·/readyz)
    app.get('/', (req: Request, res: Response) =>
    {
        res.json(ok({ service: 'd1-backend', version: '0.1.0' }));
    });

    // 라우터 마운트
    app.use('/api/auth', authRouter);

    // 라이브니스: 의존성(DB) 검사 금지 — 프로세스 생존만 본다.
    app.get('/healthz', (_req: Request, res: Response) =>
    {
        res.status(200).json({ status: 'ok' });
    });

    // 레디니스: DB 핑 성공해야 트래픽 수용 가능. 실패 시 503.
    app.get('/readyz', async (req: Request, res: Response) =>
    {
        try
        {
            await pingDb();
            res.status(200).json({ status: 'ok' });
        }
        catch (err)
        {
            (req.log ?? logger).error({ err }, 'readiness 체크 실패 — DB 도달 불가');
            res.status(503).json({ status: 'error' });
        }
    });

    /*---------------
       Error Handler
    ----------------*/

    // 404
    app.use((req: Request, res: Response) =>
    {
        res.status(Codes.NOT_FOUND.http).json(fail(Codes.NOT_FOUND.code, '요청한 경로를 찾을 수 없습니다.'));
    });

    // 에러 미들웨어 (4-arg 시그니처여야 Express가 에러 핸들러로 인식)
    app.use((err: unknown, req: Request, res: Response, next: NextFunction) =>
    {
        if (err instanceof AppError)
        {
            res.status(err.kind.http).json(fail(err.kind.code, err.message));
            return;
        }
        (req.log ?? logger).error({ err }, '처리되지 않은 예외');
        res.status(Codes.INTERNAL_ERROR.http).json(fail(Codes.INTERNAL_ERROR.code, '서버 오류가 발생했습니다.'));
    });

    return app;
}
