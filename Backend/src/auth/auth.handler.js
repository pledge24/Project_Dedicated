// 인증 요청/응답 어댑터 (handler 레이어)
import * as service from './auth.service.js';
import { ok } from '../common/envelope.js';
import { normalizeLoginId, validateLoginId, validatePassword, validateNickname } from '../common/validate.js';

/**
 * POST /api/auth/register
 * body: { loginId, password, nickname }
 */
export async function register(req, res, next)
{
    try
    {
        const body = req.body || {};
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
export async function login(req, res, next)
{
    try
    {
        const body = req.body || {};
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
export async function me(req, res, next)
{
    try
    {
        const { userId, nickname } = req.user;
        const data = await service.getMe(userId, nickname);
        res.json(ok(data));
    }
    catch (err)
    {
        next(err);
    }
}
