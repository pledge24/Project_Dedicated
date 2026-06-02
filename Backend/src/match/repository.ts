// 매칭 도메인의 DB 쿼리 (repository 레이어).
import type { RowDataPacket } from 'mysql2';

import { getPool } from '../common/db.js';

interface ScoreRow extends RowDataPacket
{
    nickname: string;
    score: number;
}

/** 큐 입장 시점의 닉네임·점수 조회. 없으면 null. */
export async function findScoreAndNickname(userId: number): Promise<{ nickname: string; score: number } | null>
{
    const [rows] = await getPool().execute<ScoreRow[]>(
        'SELECT u.nickname AS nickname, p.score AS score ' +
        'FROM users u JOIN player_profiles p ON p.user_id = u.id ' +
        'WHERE u.id = ? LIMIT 1',
        [userId]
    );
    return rows.length ? { nickname: rows[0].nickname, score: rows[0].score } : null;
}
