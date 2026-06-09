// 인증 요청/응답 어댑터 (handler 레이어)
import type { NextFunction, Request, Response } from 'express';

import { ok } from '../common/envelope.js';
import { normalizeLoginId, validateLoginId, validateNickname, validatePassword } from '../common/validate.js';
import * as service from './auth.service.js';

/**
 * POST /api/auth/register
 * body: { loginId, password, nickname }
 */
export async function register(req: Request, res: Response, next: NextFunction): Promise<void>
{
    try
    {
        const body = req.body ?? {};
        const loginId = normalizeLoginId(body.loginId);
        const { password, nickname } = body;
        validateLoginId(loginId);
        validatePassword(password);
        validateNickname(nickname);

        const data = await service.register(loginId, password, nickname);
        res.json(ok(data));
    }
    catch (err)
    {
        next(err);
    }
}

/**
 * POST /api/auth/login
 * body: { loginId, password }
 */
export async function login(req: Request, res: Response, next: NextFunction): Promise<void>
{
    try
    {
        const body = req.body ?? {};
        const loginId = normalizeLoginId(body.loginId);
        const { password } = body;
        validateLoginId(loginId);
        validatePassword(password);

        const data = await service.login(loginId, password);
        res.json(ok(data));
    }
    catch (err)
    {
        next(err);
    }
}

/**
 * GET /api/auth/me  (requireAuth 보호)
 * 저장된 토큰의 유효성 확인 + 최신 프로필(score/level/exp) 반환.
 */
export async function me(req: Request, res: Response, next: NextFunction): Promise<void>
{
    try
    {
        // requireAuth 통과 후이므로 req.user 는 항상 채워져 있다.
        const { userId, nickname } = req.user!;
        const data = await service.getMe(userId, nickname);
        res.json(ok(data));
    }
    catch (err)
    {
        next(err);
    }
}
