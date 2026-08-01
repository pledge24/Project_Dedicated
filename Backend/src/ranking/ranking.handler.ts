// 랭킹 요청/응답 어댑터 (handler 레이어). 쿼리 limit/offset 검증.
import type { Request, Response } from 'express';

import { config } from '../common/config.js';
import { ok } from '../common/envelope.js';
import { AppError, Codes } from '../common/errors.js';
import * as service from './ranking.service.js';

interface Pagination
{
    limit: number;
    offset: number;
}

/**
 * GET /api/ranking  (requireAuth 보호)
 * query: ?limit=&offset=  → score DESC 페이지.
 */
export async function getRanking(req: Request, res: Response): Promise<void>
{
    const { limit, offset } = parsePagination(req.query);
    const data = await service.getRanking(req.user!.userId, limit, offset);
    res.json(ok(data));
}

function parsePagination(query: Request['query']): Pagination
{
    return {
        limit: parseLimit(query.limit),
        offset: parseOffset(query.offset),
    };
}

/** 미지정 시 기본값, 지정 시 1 이상 정수만 허용하고 maxLimit로 클램프. */
function parseLimit(raw: unknown): number
{
    if (raw === undefined)
    {
        return config.ranking.defaultLimit;
    }
    const n = Number(raw);
    // isSafeInteger — isInteger는 1e30 같은 값도 통과시키는데, 그게 그대로 SQL에 보간되면
    // "OFFSET 1e+30" 문법 에러나 BIGINT 범위 초과로 500이 된다(repository가 값을 직접 보간).
    if (!Number.isSafeInteger(n) || n < 1)
    {
        throw new AppError(Codes.VALIDATION_FAILED, 'limit은 1 이상의 정수여야 합니다.');
    }

    return Math.min(n, config.ranking.maxLimit);
}

/** 미지정 시 0, 지정 시 0 이상 정수만 허용하고 maxOffset으로 클램프. */
function parseOffset(raw: unknown): number
{
    if (raw === undefined)
    {
        return 0;
    }
    const n = Number(raw);
    if (!Number.isSafeInteger(n) || n < 0)
    {
        throw new AppError(Codes.VALIDATION_FAILED, 'offset은 0 이상의 정수여야 합니다.');
    }

    return Math.min(n, config.ranking.maxOffset);
}
