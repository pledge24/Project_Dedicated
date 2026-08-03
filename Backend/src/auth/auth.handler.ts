// 인증 요청/응답 어댑터 (handler 레이어)
import type { Request, Response } from 'express';

import { ok } from '../common/envelope.js';
import { assertLoginId, assertNickname, assertPassword, normalizeLoginId } from '../common/validate.js';
import * as service from './auth.service.js';

/**
 * POST /api/auth/register
 * body: { loginId, password, nickname }
 */
export async function register(req: Request, res: Response): Promise<void>
{
    const body = req.body ?? {};
    const loginId = normalizeLoginId(body.loginId);
    const { password, nickname } = body;
    assertLoginId(loginId);
    assertPassword(password);
    assertNickname(nickname);

    const data = await service.register(loginId, password, nickname);
    res.json(ok(data));
}

/**
 * POST /api/auth/login
 * body: { loginId, password }
 */
export async function login(req: Request, res: Response): Promise<void>
{
    const body = req.body ?? {};
    const loginId = normalizeLoginId(body.loginId);
    const { password } = body;
    assertLoginId(loginId);
    assertPassword(password);

    const data = await service.login(loginId, password);
    res.json(ok(data));
}

/**
 * GET /api/auth/me  (requireAuth 보호)
 * 저장된 토큰의 유효성 확인 + 최신 프로필(score/level/exp) 반환.
 */
export async function me(req: Request, res: Response): Promise<void>
{
    // requireAuth 통과 후이므로 req.user 는 항상 채워져 있다.
    const { userId, nickname } = req.user!;
    const data = await service.fetchMe(userId, nickname);
    res.json(ok(data));
}

/**
 * GET /api/auth/heartbeat  (requireAuth 보호)
 * 로비 클라가 30초마다 호출. requireAuth가 token_version 대조로 대체된 세션을 401 SESSION_SUPERSEDED로 거절한다.
 * 유효하면 프로필 쿼리 없이 valid만 반환(경량).
 */
export function heartbeat(_req: Request, res: Response): void
{
    res.json(ok({ valid: true }));
}
