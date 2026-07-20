// 매치 결과 저장 (repository 레이어) — matches + match_participants + player_profiles를 한 트랜잭션으로.
// ELO는 현재 점수에 의존하므로 트랜잭션 안에서 FOR UPDATE로 점수를 잠그고 재조회한 뒤 계산한다.
import type { ResultSetHeader, RowDataPacket } from 'mysql2';
import type { PoolConnection } from 'mysql2/promise';

import { config } from '../common/config.js';
import { withTransaction } from '../common/db.js';
import type { MatchEndReason } from '../common/types.js';
import type { SettledLeaver } from './dsApi.state.js';
import { computeFfaEloDeltas } from './elo.js';

/** saveResult 함수용 - 종료 매치 각 플레이어 정보 */
export interface SaveResultParticipant
{
    userId: number;
    slotIndex: number;
    nicknameSnapshot: string;
    placement: number;
    livesLeft: number;
    left: boolean;                // 게임중 다른 기기 로그인으로 kick된 탈주자(전원 꼴등, 최하위 확정값)
    settled?: SettledLeaver;      // kick 시점에 즉시 정산됨 → 프로필 재갱신 skip, 저장값 재사용
}

/** saveResult 함수용 - 입력 매개변수 구조 */
export interface SaveResultInput
{
    matchId: string;          // client_match_id (UNIQUE → 멱등성 가드)
    mapName: string;
    startedAt: Date;
    endedAt: Date;
    durationSec: number;
    endReason: MatchEndReason;
    winnerUserId: number | null;
    participants: SaveResultParticipant[];
}

/** ELO 계산 결과 반환 구조체 */
export interface ParticipantScoreResult
{
    userId: number;
    placement: number;
    scoreBefore: number;
    scoreDelta: number;
    scoreAfter: number;
}

interface ProfileRow extends RowDataPacket
{
    user_id: number;
    score: number;
    exp: number;
}

// 등수별 경험치(1~4등). placement 범위를 벗어나면 최저값.
const PLACEMENT_EXP = [100, 70, 40, 20];

// 등수별 기본 배점(1~4등). 1·2위 양수, 4위 음수 → 매치 총합이 순양수(인플레이션).
// 3위는 0이라 ELO항이 부호를 결정(소폭 +/−). 순수 ELO(elo.ts)에 이 값을 더해 clamp한다.
const PLACEMENT_BASE_POINTS = [45, 20, 0, -35];

// 레벨당 필요 경험치(전 구간 균일). level = floor(exp / EXP_PER_LEVEL) + 1.
const EXP_PER_LEVEL = 1000;

/** 결과를 원자적으로 기록하고 ELO로 점수를 갱신한다. matchId 중복 시 ER_DUP_ENTRY를 throw. */
export async function saveResult(input: SaveResultInput): Promise<ParticipantScoreResult[]>
{
    return withTransaction(async (conn) =>
    {
        // matches 테이블에 결과 저장.
        const [matchRes] = await conn.execute<ResultSetHeader>(
            'INSERT INTO matches ' +
            '(client_match_id, map_name, started_at, ended_at, duration_sec, end_reason, winner_user_id) ' +
            'VALUES (?, ?, ?, ?, ?, ?, ?)',
            [
                input.matchId, input.mapName, input.startedAt, input.endedAt,
                input.durationSec, input.endReason, input.winnerUserId,
            ]
        );
        const matchDbId = matchRes.insertId;

        // 탈주자(left)와 완주자를 분리. ELO는 완주자끼리만, 탈주자는 이미 확정된 값 재사용(프로필 skip).
        const finishers = input.participants.filter((p) => !p.left);
        const leavers = input.participants.filter((p) => p.left);

        const rows: ComputedRow[] = [];

        // ── 완주자: 현재 점수 잠그고 완주자끼리 FFA ELO ──
        if (finishers.length > 0)
        {
            const profileByUser = await lockAndFetchProfiles(conn, finishers.map((p) => p.userId));
            const ratings = finishers.map((p) => profileByUser.get(p.userId)!.score);
            const placements = finishers.map((p) => p.placement);
            const deltas = computeFfaEloDeltas(ratings, placements, config.match.eloK);
            finishers.forEach((p, i) =>
            {
                const c = computeParticipantResult(p.placement, ratings[i], deltas[i],
                    config.match.scoreFloor, config.match.scoreCeiling, profileByUser.get(p.userId)!.exp);
                rows.push({ p, c, updateProfile: true });
            });
        }

        // ── 탈주자: kick 시점에 이미 확정. row는 저장값, 프로필 UPDATE는 skip(중복 방지) ──
        for (const p of leavers)
        {
            const delta = p.settled ? p.settled.scoreDelta : 0; // 정산 유실 시 0(패널티 escape 허용)
            const after = p.settled ? p.settled.scoreAfter : 0;
            rows.push({
                p,
                c: { scoreBefore: after - delta, scoreDelta: delta, scoreAfter: after, expGained: 0, isWin: 0, levelAfter: 0 },
                updateProfile: false,
            });
        }

        // match_participants는 multi-row INSERT 1회. VALUES 그룹만 동적 생성(주입 위험 0).
        // DB 컬럼명은 abandoned 유지(마이그레이션 생략) — 값은 left 플래그.
        const rowsSql = rows.map(() => '(?, ?, ?, ?, ?, ?, ?, ?, ?)').join(', ');
        const insertParams = rows.flatMap(({ p, c }) =>
            [matchDbId, p.userId, p.nicknameSnapshot, p.slotIndex, p.placement, p.livesLeft, c.expGained, c.scoreDelta, p.left ? 1 : 0]);
        await conn.execute(
            'INSERT INTO match_participants ' +
            '(match_id, user_id, nickname_snapshot, slot_index, placement, lives_left, exp_gained, score_delta, abandoned) ' +
            `VALUES ${rowsSql}`,
            insertParams
        );

        // player_profiles는 행마다 값이 달라 개별 UPDATE. 정산 완료 탈주자(updateProfile=false)는 skip.
        const saved: ParticipantScoreResult[] = [];
        for (const { p, c, updateProfile } of rows)
        {
            if (updateProfile)
            {
                await conn.execute(
                    'UPDATE player_profiles SET ' +
                    'score = ?, wins = wins + ?, losses = losses + ?, matches_played = matches_played + 1, ' +
                    'exp = exp + ?, level = ?, last_match_at = ?, ' +
                    // 점수가 실제로 바뀐 매치만 갱신 시점 기록 (±0은 유지) → 랭킹 동점 순위 보호.
                    'score_updated_at = CASE WHEN ? <> 0 THEN ? ELSE score_updated_at END ' +
                    'WHERE user_id = ?',
                    [c.scoreAfter, c.isWin, 1 - c.isWin, c.expGained, c.levelAfter, input.endedAt, c.scoreDelta, input.endedAt, p.userId]
                );
            }

            saved.push({
                userId: p.userId,
                placement: p.placement,
                scoreBefore: c.scoreBefore,
                scoreDelta: c.scoreDelta,
                scoreAfter: c.scoreAfter,
            });
        }

        return saved;
    });
}

/** 참가자별 파생값 + 프로필 갱신 여부. */
interface ComputedRow
{
    p: SaveResultParticipant;
    c: { scoreBefore: number; scoreDelta: number; scoreAfter: number; expGained: number; isWin: number; levelAfter: number };
    updateProfile: boolean;
}

/**
 * 탈주자 즉시 정산 — kick 시점(매치 종료 전)에 프로필 점수를 바로 하락시켜 로비에 반영되게 한다.
 * 최하위 확정값(base[꼴등] + 탈주 감점, ELO 무관)만 적용. 멱등 판단은 호출부(service).
 */
export async function settleLeaverProfile(userId: number): Promise<SettledLeaver>
{
    return withTransaction(async (conn) =>
    {
        const profileByUser = await lockAndFetchProfiles(conn, [userId]);
        const before = profileByUser.get(userId)!.score;
        const after = computeLeaverAfter(before, config.match.scoreFloor, config.match.scoreCeiling, config.match.leaverPenalty);
        const scoreDelta = after - before;
        const now = new Date();

        await conn.execute(
            'UPDATE player_profiles SET ' +
            'score = ?, losses = losses + 1, matches_played = matches_played + 1, last_match_at = ?, ' +
            'score_updated_at = CASE WHEN ? <> 0 THEN ? ELSE score_updated_at END ' +
            'WHERE user_id = ?',
            [after, now, scoreDelta, now, userId]
        );

        return { scoreDelta, scoreAfter: after };
    });
}

/** 참가자 점수·경험치를 FOR UPDATE로 잠그고 재조회. 잠근 행 수가 요청과 다르면 즉시 throw(침묵 오염 방지). */
async function lockAndFetchProfiles(conn: PoolConnection, userIds: number[]): Promise<Map<number, { score: number; exp: number }>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await conn.query<ProfileRow[]>(
        `SELECT user_id, score, exp FROM player_profiles WHERE user_id IN (${placeholders}) FOR UPDATE`,
        userIds
    );
    if (rows.length !== userIds.length)
    {
        throw new Error(`프로필 잠금 행 수 불일치: 요청 ${userIds.length}, 조회 ${rows.length}`);
    }

    const profileByUser = new Map<number, { score: number; exp: number }>();
    for (const row of rows)
    {
        profileByUser.set(Number(row.user_id), { score: row.score, exp: row.exp });
    }

    return profileByUser;
}

/** 완주자 한 명의 점수·레벨 파생값(순수). floor·ceiling 적용 후 실제 변화량을 scoreDelta로 반환 → before+delta=after 보장. */
function computeParticipantResult(placement: number, before: number, delta: number, scoreFloor: number, scoreCeiling: number, expBefore: number)
{
    // delta는 순수 ELO항. 등수 기본배점을 더한 뒤 상·하한으로 clamp.
    const after = Math.min(scoreCeiling, Math.max(scoreFloor, before + delta + basePointsForPlacement(placement)));
    const expGained = expForPlacement(placement);

    return {
        scoreBefore: before,
        scoreDelta: after - before,       // 반영된 실변화량(clamp 반영)
        scoreAfter: after,
        isWin: placement === 1 ? 1 : 0,
        expGained,
        levelAfter: levelForExp(expBefore + expGained),
    };
}

/** 탈주자 최하위 확정값(순수) — base[꼴등] + 탈주 감점, clamp. 즉시 정산 전용. */
function computeLeaverAfter(before: number, scoreFloor: number, scoreCeiling: number, leaverPenalty: number): number
{
    const lastPlacement = config.match.playersPerMatch;

    return Math.min(scoreCeiling, Math.max(scoreFloor, before + basePointsForPlacement(lastPlacement) - leaverPenalty));
}

/** 등수별 획득 경험치. 범위를 벗어나면 최저값. */
function expForPlacement(placement: number): number
{
    return PLACEMENT_EXP[placement - 1] ?? PLACEMENT_EXP[PLACEMENT_EXP.length - 1];
}

/** 등수별 기본 배점. 범위를 벗어나면 최저값(꼴찌 취급). */
function basePointsForPlacement(placement: number): number
{
    return PLACEMENT_BASE_POINTS[placement - 1] ?? PLACEMENT_BASE_POINTS[PLACEMENT_BASE_POINTS.length - 1];
}

/** 누적 경험치로 레벨 산출. 전 구간 EXP_PER_LEVEL당 1레벨 (Lv.1 = exp 0~999). */
function levelForExp(exp: number): number
{
    return Math.floor(exp / EXP_PER_LEVEL) + 1;
}
