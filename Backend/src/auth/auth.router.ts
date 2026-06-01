// 인증 라우터 (network 레이어) — rate-limit 부착
import express from 'express';
import type { Request, Response } from 'express';
import rateLimit from 'express-rate-limit';

import * as handler from './auth.handler.js';
import { requireAuth } from '../common/authMiddleware.js';
import { fail } from '../common/envelope.js';
import { Codes } from '../common/errors.js';
import { config } from '../common/config.js';

/** 공통 limit 핸들러 — envelope 포맷 응답 */
function rateLimitHandler(req: Request, res: Response): void
{
    res.status(Codes.RATE_LIMITED.http).json(fail(Codes.RATE_LIMITED.code, '요청이 너무 잦습니다. 잠시 후 다시 시도해주세요.'));
}

const loginLimiter = rateLimit({
    windowMs: config.rateLimit.windowMs,
    max: config.rateLimit.loginMax,
    standardHeaders: true,
    legacyHeaders: false,
    handler: rateLimitHandler,
});

const registerLimiter = rateLimit({
    windowMs: config.rateLimit.windowMs,
    max: config.rateLimit.registerMax,
    standardHeaders: true,
    legacyHeaders: false,
    handler: rateLimitHandler,
});

const router = express.Router();

router.post('/register', registerLimiter, handler.register);
router.post('/login',    loginLimiter,    handler.login);
router.get('/me',        requireAuth,     handler.me);

export default router;
