// Express 앱 조립: 미들웨어 → 라우터 → 에러 핸들러
const express = require('express');

const { ok, fail } = require('./common/envelope');
const { AppError } = require('./common/errors');
const authRouter = require('./auth/auth.router');

/**
 * @returns {import('express').Express}
 */
function buildApp()
{
    const app = express();

    app.use(express.json({ limit: '32kb' }));

    // 헬스체크
    app.get('/', (req, res) => res.json(ok({ service: 'd1-backend', version: '0.1.0' })));

    // 라우터 마운트
    app.use('/api/auth', authRouter);

    // 404
    app.use((req, res) => {
        res.status(404).json(fail('NOT_FOUND', '요청한 경로를 찾을 수 없습니다.'));
    });

    // 에러 미들웨어
    // eslint-disable-next-line no-unused-vars
    app.use((err, req, res, next) => {
        if (err instanceof AppError)
        {
            return res.status(err.kind.http).json(fail(err.kind.code, err.message));
        }
        console.error('[Unhandled]', err);
        return res.status(500).json(fail('INTERNAL_ERROR', '서버 오류가 발생했습니다.'));
    });

    return app;
}

module.exports = buildApp;
