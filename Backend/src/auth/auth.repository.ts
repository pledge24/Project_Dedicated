// 인증 도메인의 DB 쿼리만 담당 (repository 레이어)
import type { ResultSetHeader } from 'mysql2';

import { getPool } from '../common/db.js';
import type { UserRow, PlayerProfileRow } from '../common/types.js';

export async function findByLoginId(loginId: string): Promise<UserRow | null>
{
    const [rows] = await getPool().execute<UserRow[]>(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE login_id = ? LIMIT 1',
        [loginId]
    );
    return rows.length ? rows[0] : null;
}

export async function findByNickname(nickname: string): Promise<UserRow | null>
{
    const [rows] = await getPool().execute<UserRow[]>(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE nickname = ? LIMIT 1',
        [nickname]
    );
    return rows.length ? rows[0] : null;
}

/**
 * users INSERT + player_profiles INSERT를 한 트랜잭션으로 묶는다.
 * 둘 중 하나라도 실패하면 둘 다 롤백된다.
 */
export async function insertUser(
    { loginId, passwordHash, nickname }: { loginId: string; passwordHash: string; nickname: string }
): Promise<number>
{
    const conn = await getPool().getConnection();
    try
    {
        await conn.beginTransaction();

        const [userRes] = await conn.execute<ResultSetHeader>(
            'INSERT INTO users (login_id, password_hash, nickname) VALUES (?, ?, ?)',
            [loginId, passwordHash, nickname]
        );
        const userId = userRes.insertId;

        // player_profiles는 모든 컬럼이 DEFAULT를 갖고 있다 (score=1000, level=1, exp=0 ...).
        await conn.execute(
            'INSERT INTO player_profiles (user_id) VALUES (?)',
            [userId]
        );

        await conn.commit();
        return userId;
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

export async function findProfileByUserId(userId: number): Promise<PlayerProfileRow | null>
{
    const [rows] = await getPool().execute<PlayerProfileRow[]>(
        'SELECT user_id, score, level, exp, wins, losses, matches_played, last_match_at ' +
        'FROM player_profiles WHERE user_id = ? LIMIT 1',
        [userId]
    );
    return rows.length ? rows[0] : null;
}
