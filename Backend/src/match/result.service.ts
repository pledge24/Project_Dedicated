// 매치 결과 처리 (service 레이어) — roster 검증 + 서버 토큰 + 무결성 → 트랜잭션 저장.
// 서버 권위 모델: 결과는 DS만 보고 가능하며 매치당 발급된 serverToken으로만 통과한다.
import { isDuplicateKeyError } from '../common/db.js';
import { AppError, Codes } from '../common/errors.js';
import type { MatchResultRequest, MatchResultResponse } from '../common/types.js';
import * as repo from './result.repository.js';
import * as rosters from './roster.js';

/** 서버 토큰을 검증하고 결과를 기록한다. 실패 케이스별 AppError를 throw. */
export async function submitResult(serverToken: string, req: MatchResultRequest): Promise<MatchResultResponse>
{
    const roster = rosters.get(req.matchId);
    if (!roster)
    {
        throw new AppError(Codes.MATCH_NOT_FOUND, '해당 매치를 찾을 수 없습니다.');
    }
    if (serverToken !== roster.serverToken)
    {
        throw new AppError(Codes.INVALID_SERVER_TOKEN, '서버 토큰이 유효하지 않습니다.');
    }

    // assert 체크.
    assertResultsMatchRoster(roster.players, req.results);

    const participants = req.results.map((r) =>
    {
        const rp = roster.players.find((p) => p.userId === r.userId)!; // 위 검증으로 존재 보장

        return {
            userId: r.userId,
            slotIndex: r.slotIndex,
            nicknameSnapshot: rp.nickname,
            placement: r.placement,
            livesLeft: r.livesLeft,
        };
    });

    try
    {
        const saved = await repo.saveResult({
            matchId: req.matchId,
            mapName: req.mapName,
            startedAt: new Date(roster.startedAt),
            endedAt: new Date(),
            durationSec: req.durationSec,
            endReason: req.endReason,
            winnerUserId: soleWinner(req.results),
            participants,
        });

        return {
            matchId: req.matchId,
            participants: saved.map((s) => ({
                userId: s.userId,
                placement: s.placement,
                scoreDelta: s.scoreDelta,
                scoreAfter: s.scoreAfter,
            })),
        };
    }
    catch (err)
    {
        if (isDuplicateKeyError(err))
        {
            throw new AppError(Codes.RESULT_ALREADY_SUBMITTED, '이미 처리된 매치 결과입니다.');
        }
        throw err;
    }
}

/** 보고된 userId 집합이 roster와 정확히 일치하는지(누락·외부인·중복 없음) 검증. */
function assertResultsMatchRoster(players: rosters.RosterPlayer[], results: MatchResultRequest['results']): void
{
    const expected = new Set(players.map((p) => p.userId));
    if (results.length !== expected.size)
    {
        throw new AppError(Codes.INVALID_RESULT, '참가자 수가 매치와 일치하지 않습니다.');
    }

    const seen = new Set<number>();
    const seenSlots = new Set<number>();
    for (const r of results)
    {
        if (!expected.has(r.userId))
        {
            throw new AppError(Codes.INVALID_RESULT, '매치에 속하지 않은 참가자가 포함되어 있습니다.');
        }
        if (seen.has(r.userId))
        {
            throw new AppError(Codes.INVALID_RESULT, '참가자 userId가 중복되었습니다.');
        }
        seen.add(r.userId);

        // 좌석(slotIndex)은 DS가 배정한 권위값 — 매치 내 유일해야 한다(DB UNIQUE와 일치).
        if (seenSlots.has(r.slotIndex))
        {
            throw new AppError(Codes.INVALID_RESULT, 'slotIndex가 중복되었습니다.');
        }
        seenSlots.add(r.slotIndex);
    }
}

/** 단독 1위가 있으면 그 userId, 동률 1위거나 없으면 null. */
function soleWinner(results: MatchResultRequest['results']): number | null
{
    const firsts = results.filter((r) => r.placement === 1);

    return firsts.length === 1 ? firsts[0].userId : null;
}
