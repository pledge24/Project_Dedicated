// Bearer 토큰 검증 미들웨어 (network 레이어). Authorization 헤더 → req.user.
import type { NextFunction, Request, Response } from 'express';

import { extractBearerToken } from './bearer.js';
import { AppError, Codes } from './errors.js';
import * as jwt from './jwt.js';
import { getCurrentTokenVersion } from './session.js';

/**
 * Authorization: Bearer <token> 를 검증하고 req.user를 채운다.
 * 실패 시 AppError를 next로 넘겨 중앙 에러 미들웨어가 봉투 응답을 만든다.
 * 단일 세션 강제: 서명·만료 검증 후 토큰의 tokenVersion을 DB 현재값과 대조한다.
 * (Express 5는 async 미들웨어의 reject를 자동으로 에러 핸들러로 포워드한다.)
 */
export async function requireAuth(req: Request, res: Response, next: NextFunction): Promise<void>
{
    const token = extractBearerToken(req.headers.authorization);
    if (!token)
    {
        return next(new AppError(Codes.AUTH_REQUIRED, '인증이 필요합니다.'));
    }

    let payload;
    try
    {
        payload = jwt.verify(token);
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

    // 더 최신 로그인이 있었으면(버전 불일치) 이 토큰은 무효 — 다른 기기 로그인으로 세션 대체됨.
    const currentVersion = await getCurrentTokenVersion(payload.userId);
    if (currentVersion === null || currentVersion !== payload.tokenVersion)
    {
        return next(new AppError(Codes.SESSION_SUPERSEDED, '다른 기기에서 로그인되어 세션이 종료되었습니다.'));
    }

    req.user = { userId: payload.userId, nickname: payload.nickname };
    next();
}
