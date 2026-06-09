// 매치 결과 저장 (repository 레이어) — matches + match_participants + player_profiles를 한 트랜잭션으로.
// ELO는 현재 점수에 의존하므로 트랜잭션 안에서 FOR UPDATE로 점수를 잠그고 재조회한 뒤 계산한다.
import type { ResultSetHeader, RowDataPacket } from 'mysql2';

import { getPool } from '../common/db.js';
import type { MatchEndReason } from '../common/types.js';
import { computeFfaEloDeltas } from './elo.js';

export interface SaveResultParticipant
{
    userId: number;
    slotIndex: number;
    nicknameSnapshot: string;
    placement: number;
    livesLeft: number;
}

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
    eloK: number;
    scoreFloor: number;
}

export interface SavedParticipant
{
    userId: number;
    placement: number;
    scoreBefore: number;
    scoreDelta: number;
    scoreAfter: number;
}

interface ScoreRow extends RowDataPacket
{
    user_id: number;
    score: number;
}

// 등수별 경험치(1~4등). placement 범위를 벗어나면 최저값.
const PLACEMENT_EXP = [100, 70, 40, 20];

/** 결과를 원자적으로 기록하고 ELO로 점수를 갱신한다. matchId 중복 시 ER_DUP_ENTRY를 throw. */
export async function saveResult(input: SaveResultInput): Promise<SavedParticipant[]>
{
    const conn = await getPool().getConnection();
    try
    {
        await conn.beginTransaction();

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

        // 현재 점수를 잠그고(FOR UPDATE) 재조회 — roster의 점수는 stale일 수 있다.
        const userIds = input.participants.map((p) => p.userId);
        const placeholders = userIds.map(() => '?').join(', ');
        const [scoreRows] = await conn.query<ScoreRow[]>(
            `SELECT user_id, score FROM player_profiles WHERE user_id IN (${placeholders}) FOR UPDATE`,
            userIds
        );
        const scoreByUser = new Map<number, number>();
        for (const row of scoreRows)
        {
            scoreByUser.set(Number(row.user_id), row.score);
        }

        const ratings = input.participants.map((p) => scoreByUser.get(p.userId) ?? 0);
        const placements = input.participants.map((p) => p.placement);
        const deltas = computeFfaEloDeltas(ratings, placements, input.eloK);

        const saved: SavedParticipant[] = [];
        for (let i = 0; i < input.participants.length; i++)
        {
            const p = input.participants[i];
            const before = ratings[i];
            const delta = deltas[i];
            const after = Math.max(input.scoreFloor, before + delta);
            const isWin = p.placement === 1 ? 1 : 0;
            const expGained = PLACEMENT_EXP[p.placement - 1] ?? PLACEMENT_EXP[PLACEMENT_EXP.length - 1];

            await conn.execute(
                'INSERT INTO match_participants ' +
                '(match_id, user_id, nickname_snapshot, slot_index, placement, lives_left, exp_gained, score_delta) ' +
                'VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                [matchDbId, p.userId, p.nicknameSnapshot, p.slotIndex, p.placement, p.livesLeft, expGained, delta]
            );

            await conn.execute(
                'UPDATE player_profiles SET ' +
                'score = ?, wins = wins + ?, losses = losses + ?, matches_played = matches_played + 1, ' +
                'exp = exp + ?, last_match_at = ? WHERE user_id = ?',
                [after, isWin, 1 - isWin, expGained, input.endedAt, p.userId]
            );

            saved.push({ userId: p.userId, placement: p.placement, scoreBefore: before, scoreDelta: delta, scoreAfter: after });
        }

        await conn.commit();

        return saved;
    }
    catch (err)
    {
        await conn.rollback();
        throw err;
    }
    finally
    {
        conn.release();
    }
}
