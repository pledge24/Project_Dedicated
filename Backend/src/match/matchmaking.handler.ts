// 플레이어용 매치 요청 어댑터 (handler 레이어). DS용은 dsApi.* 로 분리돼 있다.
import type { Request, Response } from 'express';

import { ok } from '../common/envelope.js';
import * as service from './matchmaking.service.js';

/**
 * GET /api/match/current  (requireAuth 보호)
 * 진행 중인 내 매치가 있으면 재입장 주소를, 없으면 { active: false }.
 * 클라는 로그인 직후 1회 호출해 끊겼던 매치로 되돌아간다.
 */
export async function current(req: Request, res: Response): Promise<void>
{
    // requireAuth 통과 후이므로 req.user 는 항상 채워져 있다.
    const { userId } = req.user!;
    const match = await service.findRejoinableMatch(userId);

    res.json(ok(match ? { active: true, ...match } : { active: false }));
}
