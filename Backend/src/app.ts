// Express 앱 조립: 미들웨어 → 라우터 → 에러 핸들러
import express from 'express';
import type { Request, Response, NextFunction, Express } from 'express';

import { ok, fail } from './common/envelope.js';
import { AppError, Codes } from './common/errors.js';
import authRouter from './auth/auth.router.js';

export default function buildApp(): Express
{
    const app = express();

    app.use(express.json({ limit: '32kb' }));

    // 헬스체크
    app.get('/', (req: Request, res: Response) =>
    {
        res.json(ok({ service: 'd1-backend', version: '0.1.0' }));
    });

    // 라우터 마운트
    app.use('/api/auth', authRouter);

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
        console.error('[Unhandled]', err);
        res.status(Codes.INTERNAL_ERROR.http).json(fail(Codes.INTERNAL_ERROR.code, '서버 오류가 발생했습니다.'));
    });

    return app;
}
