// 매치 결과 저장 (repository 레이어) — matches + match_participants + player_profiles를 한 트랜잭션으로.
// ELO는 현재 점수에 의존하므로 트랜잭션 안에서 FOR UPDATE로 점수를 잠그고 재조회한 뒤 계산한다.
import type { ResultSetHeader, RowDataPacket } from 'mysql2';
import type { PoolConnection } from 'mysql2/promise';

import { config } from '../common/config.js';
import { withTransaction } from '../common/db.js';
import type { MatchEndReason } from '../common/types.js';
import { computeFfaEloDeltas } from './elo.js';

/** saveResult 함수용 - 종료 매치 각 플레이어 정보 */
export interface SaveResultParticipant
{
    userId: number;
    slotIndex: number;
    nicknameSnapshot: string;
    placement: number;
    livesLeft: number;
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

        // 현재 점수·경험치를 잠그고(FOR UPDATE) 재조회 — roster의 값은 stale일 수 있다.
        const userIds = input.participants.map((p) => p.userId);
        const profileByUser = await lockAndFetchProfiles(conn, userIds);

        const ratings = input.participants.map((p) => profileByUser.get(p.userId)!.score);
        const placements = input.participants.map((p) => p.placement);
        const deltas = computeFfaEloDeltas(ratings, placements, config.match.eloK);

        // 참가자별 파생값을 먼저 확정 (순수) — INSERT/UPDATE 값이 모두 여기서 나온다.
        const computed = input.participants.map((p, i) => ({
            p,
            c: computeParticipantResult(p.placement, ratings[i], deltas[i], config.match.scoreFloor, profileByUser.get(p.userId)!.exp),
        }));

        // match_participants는 multi-row INSERT 1회. VALUES 그룹만 동적 생성 —
        // 사용자 데이터가 아니라 '(?, ...)' 텍스트라 주입 위험 0 (동적 IN 선례와 동일).
        const rowsSql = computed.map(() => '(?, ?, ?, ?, ?, ?, ?, ?)').join(', ');
        const insertParams = computed.flatMap(({ p, c }) =>
            [matchDbId, p.userId, p.nicknameSnapshot, p.slotIndex, p.placement, p.livesLeft, c.expGained, c.scoreDelta]);
        await conn.execute(
            'INSERT INTO match_participants ' +
            '(match_id, user_id, nickname_snapshot, slot_index, placement, lives_left, exp_gained, score_delta) ' +
            `VALUES ${rowsSql}`,
            insertParams
        );

        // player_profiles는 행마다 값이 달라 개별 UPDATE 유지.
        const saved: ParticipantScoreResult[] = [];
        for (const { p, c } of computed)
        {
            await conn.execute(
                'UPDATE player_profiles SET ' +
                'score = ?, wins = wins + ?, losses = losses + ?, matches_played = matches_played + 1, ' +
                'exp = exp + ?, level = ?, last_match_at = ? WHERE user_id = ?',
                [c.scoreAfter, c.isWin, 1 - c.isWin, c.expGained, c.levelAfter, input.endedAt, p.userId]
            );

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

/** 한 참가자의 점수·레벨 파생값(순수). floor 적용 후 실제 변화량을 scoreDelta로 반환 → before+delta=after 보장. */
function computeParticipantResult(placement: number, before: number, delta: number, scoreFloor: number, expBefore: number)
{
    const after = Math.max(scoreFloor, before + delta);
    const expGained = expForPlacement(placement);

    return {
        scoreBefore: before,
        scoreDelta: after - before,       // A3: 반영된 실변화량(floor 반영)
        scoreAfter: after,
        isWin: placement === 1 ? 1 : 0,
        expGained,
        levelAfter: levelForExp(expBefore + expGained),
    };
}

/** 등수별 획득 경험치. 범위를 벗어나면 최저값. */
function expForPlacement(placement: number): number
{
    return PLACEMENT_EXP[placement - 1] ?? PLACEMENT_EXP[PLACEMENT_EXP.length - 1];
}

/** 누적 경험치로 레벨 산출. 전 구간 EXP_PER_LEVEL당 1레벨 (Lv.1 = exp 0~999). */
function levelForExp(exp: number): number
{
    return Math.floor(exp / EXP_PER_LEVEL) + 1;
}
