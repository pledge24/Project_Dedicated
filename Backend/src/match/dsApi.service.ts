// DS→백엔드 요청 처리 (service 레이어) — 결과 보고 + kick 폴링 + 탈주 즉시 정산.
// 서버 권위 모델: DS만 매치당 serverToken으로 접근한다.
import { timingSafeEqual } from 'node:crypto';

import { isDuplicateKeyError } from '../common/db.js';
import { AppError, Codes } from '../common/errors.js';
import { logger } from '../common/logger.js';
import type { MatchResultRequest, MatchResultResponse } from '../common/types.js';
import * as state from './dsApi.state.js';
import * as readiness from './readiness.js';
import type { SettledLeaver } from './result.repository.js';
import * as repo from './result.repository.js';
import * as rosters from './roster.service.js';

/** serverToken을 검증하고 roster를 반환. 실패 시 AppError. */
function assertServerToken(serverToken: string, matchId: string): rosters.MatchRoster
{
    const roster = rosters.get(matchId);
    if (!roster)
    {
        throw new AppError(Codes.MATCH_NOT_FOUND, '해당 매치를 찾을 수 없습니다.');
    }
    if (!tokensEqual(serverToken, roster.serverToken))
    {
        throw new AppError(Codes.INVALID_SERVER_TOKEN, '서버 토큰이 유효하지 않습니다.');
    }

    return roster;
}

/**
 * 상수 시간 비교 — `!==`는 첫 불일치 바이트에서 끊겨 비교 시간이 일치 접두사 길이에 비례한다.
 * 토큰은 매치별 랜덤 24바이트라 현실적 위험은 낮지만, 이 값 하나가 서버 권위 전체의 근거이므로
 * 타이밍 채널을 남겨둘 이유가 없다. 길이가 다르면 timingSafeEqual이 throw하므로 먼저 거른다
 * (길이 노출은 무해 — 토큰 길이는 고정이고 공개 정보다).
 */
function tokensEqual(given: string, expected: string): boolean
{
    const a = Buffer.from(given, 'utf8');
    const b = Buffer.from(expected, 'utf8');

    return a.length === b.length && timingSafeEqual(a, b);
}

/**
 * DS 통지(POST /ready): 서버 토큰 검증 후 준비 대기 gate를 resolve(멱등).
 * 확정 후 재전송은 roster가 남아있어 통과 + signal no-op → 200. 취소/타임아웃 후엔 roster 제거돼 404(늦은 DS에 "이미 늦음").
 */
export function markServerReady(serverToken: string, matchId: string): void
{
    assertServerToken(serverToken, matchId);
    readiness.signal(matchId);
}

/** 서버 토큰을 검증하고 결과를 기록한다. 실패 케이스별 AppError를 throw. */
export async function submitResult(serverToken: string, req: MatchResultRequest): Promise<MatchResultResponse>
{
    const roster = assertServerToken(serverToken, req.matchId);

    // assert 체크.
    assertResultsMatchRoster(roster.players, req.results);

    const participants = buildParticipants(roster, req.results);

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

        // 매치 종료 — kick 대기열 정리. 재제출은 client_match_id UNIQUE로 멱등(409)이라 무해.
        // (탈주 이중 정산은 원장 match_leaver_settlements가 막으므로 resultSubmitted 플래그 불필요.)
        state.clear(req.matchId);

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
export async function settleLeaver(serverToken: string, matchId: string, userId: number): Promise<SettledLeaver>
{
    const roster = assertServerToken(serverToken, matchId);
    if (!roster.players.some((p) => p.userId === userId))
    {
        throw new AppError(Codes.INVALID_RESULT, '매치에 속하지 않은 참가자입니다.');
    }

    // 멱등·경합 안전(재시도·순서역전·결과보다 늦게 도착 포함)은 원장 match_leaver_settlements +
    // 프로필 FOR UPDATE가 보장한다 → 여기선 그대로 위임. 이미 정산됐으면 저장값을 그대로 돌려준다.
    return repo.settleLeaverProfile(matchId, userId);
}

/**
 * 보고된 항목이 roster의 부분집합인지(외부인·중복 없음) + 값이 정원 범위인지 검증.
 * 누락을 허용하는 이유: DS가 미입장자를 못 실어 보내도 정상 플레이한 나머지 인원의 결과는 남아야 한다.
 * 빠진 인원은 buildParticipants가 최하위 미참가자로 채운다.
 */
function assertResultsMatchRoster(players: rosters.RosterPlayer[], results: MatchResultRequest['results']): void
{
    const expected = new Set(players.map((p) => p.userId));
    const seatCount = players.length;
    if (results.length > seatCount)
    {
        throw new AppError(Codes.INVALID_RESULT, '참가자 수가 매치 정원을 넘습니다.');
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

        // 상한은 전역 상수가 아니라 이 매치의 정원 — handler는 roster를 모르므로 여기서 본다.
        if (r.slotIndex > seatCount - 1)
        {
            throw new AppError(Codes.INVALID_RESULT, `slotIndex는 0~${seatCount - 1} 정수여야 합니다.`);
        }
        if (r.placement > seatCount)
        {
            throw new AppError(Codes.INVALID_RESULT, `placement는 1~${seatCount} 정수여야 합니다.`);
        }

        // 좌석(slotIndex)은 DS가 배정한 권위값 — 매치 내 유일해야 한다(DB UNIQUE와 일치).
        if (seenSlots.has(r.slotIndex))
        {
            throw new AppError(Codes.INVALID_RESULT, 'slotIndex가 중복되었습니다.');
        }
        seenSlots.add(r.slotIndex);
    }
}

/**
 * roster 전원의 참가 기록을 만든다. 보고에 없는 인원은 최하위 미참가자로 채운다.
 * DS도 같은 보정을 하므로(AppendNoShowResults) 정상 경로에서는 발동하지 않는 안전망이다 —
 * 구버전 DS나 DS 측 roster 파싱 실패까지 덮는다.
 */
function buildParticipants(roster: rosters.MatchRoster, results: MatchResultRequest['results']): repo.SaveResultParticipant[]
{
    const reported = new Map(results.map((r) => [r.userId, r]));
    const usedSlots = new Set(results.map((r) => r.slotIndex));
    const lastPlacement = roster.players.length;
    let nextFreeSlot = 0;

    return roster.players.map((p) =>
    {
        const r = reported.get(p.userId);
        if (r)
        {
            return {
                userId: p.userId,
                slotIndex: r.slotIndex,
                nicknameSnapshot: p.nickname,
                placement: r.placement,
                livesLeft: r.livesLeft,
                left: r.left ?? false,
                // 봇전 봇: DB 미존재라 프로필/participants 기록 skip, ELO 입력엔 roster의 rating 사용.
                bot: p.bot ?? false,
                rating: p.rating,
            };
        }

        // 좌석이 배정된 적 없는 미참가자 — 빈 자리를 준다((match_id, slot_index) UNIQUE).
        while (usedSlots.has(nextFreeSlot))
        {
            nextFreeSlot += 1;
        }
        usedSlots.add(nextFreeSlot);
        logger.warn({ matchId: roster.matchId, userId: p.userId, slotIndex: nextFreeSlot },
            '결과 미보고 참가자 — 최하위 미참가로 기록');

        // left=true라야 완주자 ELO에서 빠져 정상 플레이한 인원끼리만 점수가 오간다.
        return {
            userId: p.userId,
            slotIndex: nextFreeSlot,
            nicknameSnapshot: p.nickname,
            placement: lastPlacement,
            livesLeft: 0,
            left: true,
            bot: p.bot ?? false,
            rating: p.rating,
        };
    });
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
