// DS→백엔드 요청 처리 (service 레이어) — 결과 보고 + kick 폴링 + 탈주 즉시 정산.
// 서버 권위 모델: DS만 매치당 serverToken으로 접근한다.
import { isDuplicateKeyError } from '../common/db.js';
import { AppError, Codes } from '../common/errors.js';
import type { MatchResultRequest, MatchResultResponse } from '../common/types.js';
import * as state from './dsApi.state.js';
import * as repo from './result.repository.js';
import * as rosters from './roster.js';

/** serverToken을 검증하고 roster를 반환. 실패 시 AppError. */
function assertServerToken(serverToken: string, matchId: string): rosters.MatchRoster
{
    const roster = rosters.get(matchId);
    if (!roster)
    {
        throw new AppError(Codes.MATCH_NOT_FOUND, '해당 매치를 찾을 수 없습니다.');
    }
    if (serverToken !== roster.serverToken)
    {
        throw new AppError(Codes.INVALID_SERVER_TOKEN, '서버 토큰이 유효하지 않습니다.');
    }

    return roster;
}

/** 서버 토큰을 검증하고 결과를 기록한다. 실패 케이스별 AppError를 throw. */
export async function submitResult(serverToken: string, req: MatchResultRequest): Promise<MatchResultResponse>
{
    const roster = assertServerToken(serverToken, req.matchId);

    // assert 체크.
    assertResultsMatchRoster(roster.players, req.results);

    const participants = req.results.map((r) =>
    {
        const rp = roster.players.find((p) => p.userId === r.userId)!; // 위 검증으로 존재 보장
        const left = r.left ?? false;

        return {
            userId: r.userId,
            slotIndex: r.slotIndex,
            nicknameSnapshot: rp.nickname,
            placement: r.placement,
            livesLeft: r.livesLeft,
            left,
            // 봇전 봇: DB 미존재라 프로필/participants 기록 skip, ELO 입력엔 roster의 rating 사용.
            bot: rp.bot ?? false,
            rating: rp.rating,
            // 이미 즉시 정산된 탈주자면 그 값을 넘겨 결과 저장 시 프로필 중복 갱신을 막는다.
            settled: left ? state.getSettled(req.matchId, r.userId) : undefined,
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
            winnerUserId: resolveWinner(roster.players, req.results),
            participants,
        });

        // 매치 종료 — 이탈 채널 상태 정리(kick 대기열 + 정산 기록). 재제출은 멱등(409)이라 무해.
        state.clear(req.matchId);
        // 결과 확정 표시 → 이후 도착한 /leaver 정산을 거부(이중 패널티 차단). roster는 sweep 전까지 유지.
        roster.resultSubmitted = true;

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

/** DS 폴링(GET /kicks): 서버 토큰 검증 후 이 매치의 kick 대기 userId 목록. */
export function listPendingKicks(serverToken: string, matchId: string): number[]
{
    assertServerToken(serverToken, matchId);

    return state.listKicks(matchId);
}

/** DS 통지(POST /leaver): 탈주자 점수를 최하위 확정값으로 즉시 정산(멱등). */
export async function settleLeaver(serverToken: string, matchId: string, userId: number): Promise<state.SettledLeaver>
{
    const roster = assertServerToken(serverToken, matchId);
    if (!roster.players.some((p) => p.userId === userId))
    {
        throw new AppError(Codes.INVALID_RESULT, '매치에 속하지 않은 참가자입니다.');
    }

    // 결과 확정 후 도착한 탈주 정산은 무시(순서 역전·재시도 시 이중 패널티 차단).
    if (roster.resultSubmitted)
    {
        return { scoreDelta: 0, scoreAfter: 0 };
    }

    // 이미 정산됐으면 그 값을 그대로 반환(DS 재시도·중복 폴링 방어).
    const existing = state.getSettled(matchId, userId);
    if (existing)
    {
        return existing;
    }

    const info = await repo.settleLeaverProfile(userId);
    state.markSettled(matchId, userId, info);

    return info;
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

/** matches.winner_user_id 값 — 단독 1위 userId. 승자가 봇전 봇이면 null(FK는 실제 유저만 참조). */
function resolveWinner(players: rosters.RosterPlayer[], results: MatchResultRequest['results']): number | null
{
    const winner = soleWinner(results);
    if (winner === null)
    {
        return null;
    }
    const rp = players.find((p) => p.userId === winner);

    return rp && rp.bot ? null : winner;
}
