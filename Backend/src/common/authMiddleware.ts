// Bearer 토큰 검증 미들웨어 (network 레이어). Authorization 헤더 → req.user.
import type { NextFunction, Request, Response } from 'express';

import { extractBearerToken } from './bearer.js';
import { AppError, Codes } from './errors.js';
import * as jwtUtil from './jwt.js';

/**
 * Authorization: Bearer <token> 를 검증하고 req.user를 채운다.
 * 실패 시 AppError를 next로 넘겨 중앙 에러 미들웨어가 봉투 응답을 만든다.
 */
export function requireAuth(req: Request, res: Response, next: NextFunction): void
{
    const token = extractBearerToken(req.headers.authorization);
    if (!token)
    {
        return next(new AppError(Codes.AUTH_REQUIRED, '인증이 필요합니다.'));
    }

    try
    {
        const payload = jwtUtil.verify(token);
        // 토큰 클레임만 신뢰. 최신 프로필이 필요한 라우트는 핸들러에서 재조회한다.
        req.user = { userId: payload.userId, nickname: payload.nickname };
        next();
    }
    catch (err)
    {
        // jsonwebtoken 에러: 만료는 별도 코드로, 그 외(위변조·서명불일치·형식오류)는 INVALID_TOKEN.
        if (err instanceof Error && err.name === 'TokenExpiredError')
        {
            return next(new AppError(Codes.TOKEN_EXPIRED, '세션이 만료되었습니다. 다시 로그인해주세요.'));
        }

        return next(new AppError(Codes.INVALID_TOKEN, '인증 토큰이 올바르지 않습니다.'));
    }
}
