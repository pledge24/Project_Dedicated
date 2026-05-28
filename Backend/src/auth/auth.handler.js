// 인증 요청/응답 어댑터 (handler 레이어)
import * as service from './auth.service.js';
import { ok } from '../common/envelope.js';
import { validateLoginId, validatePassword, validateNickname } from '../common/validate.js';

/**
 * POST /api/auth/register
 * body: { loginId, password, nickname }
 */
export async function register(req, res, next)
{
    try
    {
        const { loginId, password, nickname } = req.body || {};
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
        const { loginId, password } = req.body || {};
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
