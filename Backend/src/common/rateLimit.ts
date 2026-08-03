// rate-limit 미들웨어 팩토리 — envelope 포맷 응답 핸들러 공유.
import type { Request, Response } from 'express';
import rateLimit from 'express-rate-limit';

import { config } from './config.js';
import { fail } from './envelope.js';
import { Codes } from './errors.js';

/** 공통 limit 핸들러 — envelope 포맷 응답 */
function rateLimitHandler(req: Request, res: Response): void
{
    res.status(Codes.RATE_LIMITED.http).json(fail(Codes.RATE_LIMITED.code, '요청이 너무 잦습니다. 잠시 후 다시 시도해주세요.'));
}

/** limit 요청/windowMs 제한 미들웨어 생성. windowMs는 전역 설정을 공유한다. */
export function createRateLimiter(limit: number)
{
    return rateLimit({
        windowMs: config.rateLimit.windowMs,
        limit,
        standardHeaders: true,
        legacyHeaders: false,
        handler: rateLimitHandler,
    });
}
