// 매치 결과 요청/응답 어댑터 (handler 레이어). 본문 형식 검증 + 서버 토큰 추출.
import type { Request, Response } from 'express';

import { extractBearerToken } from '../common/bearer.js';
import { config } from '../common/config.js';
import { ok } from '../common/envelope.js';
import { AppError, Codes } from '../common/errors.js';
import type { MatchEndReason, MatchResultEntryInput, MatchResultRequest } from '../common/types.js';
import { isInt } from '../common/validate.js';
import * as service from './result.service.js';

const END_REASONS: readonly MatchEndReason[] = ['winner', 'draw', 'time_expired', 'abort'];

/**
 * POST /api/match/result  (DS만 — Authorization: Bearer <serverToken>)
 * body: { matchId, mapName, durationSec, endReason, results[] }
 */
export async function submitResult(req: Request, res: Response): Promise<void>
{
    const serverToken = extractServerToken(req);
    const body = parseResultBody(req.body);

    const data = await service.submitResult(serverToken, body);
    res.json(ok(data));
}

/** Authorization 헤더의 Bearer 토큰(=서버 토큰). 없으면 401. */
function extractServerToken(req: Request): string
{
    const token = extractBearerToken(req.headers.authorization);
    if (!token)
    {
        throw new AppError(Codes.SERVER_AUTH_REQUIRED, '서버 토큰이 필요합니다.');
    }

    return token;
}

/** 본문을 검증해 MatchResultRequest로 변환. 형식 위반 시 INVALID_RESULT. */
function parseResultBody(raw: unknown): MatchResultRequest
{
    const body = (raw ?? {}) as Record<string, unknown>;

    const matchId = body.matchId;
    const mapName = body.mapName;
    const durationSec = body.durationSec;
    const endReason = body.endReason;

    if (typeof matchId !== 'string' || matchId.length === 0)
    {
        throw new AppError(Codes.INVALID_RESULT, 'matchId가 필요합니다.');
    }
    if (typeof mapName !== 'string' || mapName.length === 0)
    {
        throw new AppError(Codes.INVALID_RESULT, 'mapName이 필요합니다.');
    }
    if (typeof durationSec !== 'number' || !Number.isFinite(durationSec) || durationSec < 0)
    {
        throw new AppError(Codes.INVALID_RESULT, 'durationSec은 0 이상의 숫자여야 합니다.');
    }
    if (typeof endReason !== 'string' || !END_REASONS.includes(endReason as MatchEndReason))
    {
        throw new AppError(Codes.INVALID_RESULT, `endReason은 ${END_REASONS.join('/')} 중 하나여야 합니다.`);
    }
    if (!Array.isArray(body.results) || body.results.length !== config.match.playersPerMatch)
    {
        throw new AppError(Codes.INVALID_RESULT, `results는 ${config.match.playersPerMatch}명이어야 합니다.`);
    }

    const n = body.results.length;
    const results = body.results.map((r) => parseEntry(r, n));

    return {
        matchId,
        mapName,
        durationSec,
        endReason: endReason as MatchEndReason,
        results,
    };
}

function parseEntry(raw: unknown, n: number): MatchResultEntryInput
{
    const e = (raw ?? {}) as Record<string, unknown>;
    const userId = e.userId;
    const slotIndex = e.slotIndex;
    const placement = e.placement;
    const livesLeft = e.livesLeft;

    if (!isInt(userId, 1))
    {
        throw new AppError(Codes.INVALID_RESULT, 'userId는 양의 정수여야 합니다.');
    }
    if (!isInt(slotIndex, 0, n - 1))
    {
        throw new AppError(Codes.INVALID_RESULT, `slotIndex는 0~${n - 1} 정수여야 합니다.`);
    }
    if (!isInt(placement, 1, n))
    {
        throw new AppError(Codes.INVALID_RESULT, `placement는 1~${n} 정수여야 합니다.`);
    }
    if (!isInt(livesLeft, 0))
    {
        throw new AppError(Codes.INVALID_RESULT, 'livesLeft는 0 이상의 정수여야 합니다.');
    }

    return { userId, slotIndex, placement, livesLeft };
}
