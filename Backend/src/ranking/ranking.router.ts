// 랭킹 라우터 (network 레이어) — rate-limit + requireAuth 부착
import express from 'express';
import type { Request, Response } from 'express';
import rateLimit from 'express-rate-limit';

import { requireAuth } from '../common/authMiddleware.js';
import { config } from '../common/config.js';
import { fail } from '../common/envelope.js';
import { Codes } from '../common/errors.js';
import * as handler from './ranking.handler.js';

/** 공통 limit 핸들러 — envelope 포맷 응답 */
function rateLimitHandler(req: Request, res: Response): void
{
    res.status(Codes.RATE_LIMITED.http).json(fail(Codes.RATE_LIMITED.code, '요청이 너무 잦습니다. 잠시 후 다시 시도해주세요.'));
}

const rankingLimiter = rateLimit({
    windowMs: config.rateLimit.windowMs,
    max: config.rateLimit.rankingMax,
    standardHeaders: true,
    legacyHeaders: false,
    handler: rateLimitHandler,
});

const router = express.Router();

router.get('/', rankingLimiter, requireAuth, handler.getRanking);

export default router;
