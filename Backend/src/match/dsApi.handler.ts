// DS→백엔드 요청 어댑터 (handler 레이어). DS만 — Authorization: Bearer <serverToken>.
import type { Request, Response } from 'express';

import { extractBearerToken } from '../common/bearer.js';
import { config } from '../common/config.js';
import { ok } from '../common/envelope.js';
import { AppError, Codes } from '../common/errors.js';
import type { MatchEndReason, MatchResultEntryInput, MatchResultRequest } from '../common/types.js';
import { isInt } from '../common/validate.js';
import * as service from './dsApi.service.js';

const END_REASONS: readonly MatchEndReason[] = ['winner', 'draw', 'time_expired', 'abort'];

/**
 * POST /api/match/result  (DS만)
 * body: { matchId, mapName, durationSec, endReason, results[] }
 */
export async function submitResult(req: Request, res: Response): Promise<void>
{
    const serverToken = extractServerToken(req);
    const body = parseResultBody(req.body);

    const data = await service.submitResult(serverToken, body);
    res.json(ok(data));
}

/**
 * POST /api/match/:matchId/ready  (DS만)
 * DS가 맵 빌드·초기화를 마치고 플레이어를 받을 준비가 됐음을 통지. 백엔드는 이걸 받고 클라에 match:found 전송.
 */
export function reportReady(req: Request, res: Response): void
{
    const serverToken = extractServerToken(req);
    const matchId = typeof req.params.matchId === 'string' ? req.params.matchId : '';
    service.markServerReady(serverToken, matchId);
    res.json(ok({}));
}

/**
 * GET /api/match/:matchId/kicks  (DS만)
 * 이 매치에서 강제 회수(다른 기기 로그인)해야 할 userId 목록. DS가 5초 폴링해 kick한다.
 */
export function getKicks(req: Request, res: Response): void
{
    const serverToken = extractServerToken(req);
    const matchId = typeof req.params.matchId === 'string' ? req.params.matchId : '';
    const userIds = service.listPendingKicks(serverToken, matchId);
    res.json(ok({ userIds }));
}

/**
 * POST /api/match/:matchId/leaver  (DS만)
 * body: { userId } — kick 즉시 탈주자 점수를 최하위 확정값으로 정산(하락). 매치 종료 전 로비 반영 경로.
 */
export async function submitLeaver(req: Request, res: Response): Promise<void>
{
    const serverToken = extractServerToken(req);
    const matchId = typeof req.params.matchId === 'string' ? req.params.matchId : '';

    const body = (req.body ?? {}) as Record<string, unknown>;
    if (!isInt(body.userId, 1))
    {
        throw new AppError(Codes.INVALID_RESULT, 'userId는 양의 정수여야 합니다.');
    }

    const settled = await service.settleLeaver(serverToken, matchId, body.userId);
    res.json(ok({ userId: body.userId, scoreDelta: settled.scoreDelta, scoreAfter: settled.scoreAfter }));
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
    const left = e.left;

    // userId: 실제 유저는 양수, 봇전 봇은 음수 sentinel. 위조는 service의 roster 대조로 차단하므로 여기선 0만 거부.
    if (typeof userId !== 'number' || !Number.isInteger(userId) || userId === 0)
    {
        throw new AppError(Codes.INVALID_RESULT, 'userId는 0이 아닌 정수여야 합니다.');
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
    if (left !== undefined && typeof left !== 'boolean')
    {
        throw new AppError(Codes.INVALID_RESULT, 'left는 boolean이어야 합니다.');
    }

    return { userId, slotIndex, placement, livesLeft, left: left === true };
}
