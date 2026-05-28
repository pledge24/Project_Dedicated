// 인증 도메인의 DB 쿼리만 담당 (repository 레이어)
import { getPool } from '../common/db.js';

/** @typedef {import('../common/types.js').UserRow} UserRow */
/** @typedef {import('../common/types.js').PlayerProfileRow} PlayerProfileRow */

/**
 * @param {string} loginId
 * @returns {Promise<UserRow|null>}
 */
export async function findByLoginId(loginId)
{
    const [rows] = await getPool().execute(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE login_id = ? LIMIT 1',
        [loginId]
    );
    return rows.length ? rows[0] : null;
}

/**
 * @param {string} nickname
 * @returns {Promise<UserRow|null>}
 */
export async function findByNickname(nickname)
{
    const [rows] = await getPool().execute(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE nickname = ? LIMIT 1',
        [nickname]
    );
    return rows.length ? rows[0] : null;
}

/**
 * users INSERT + player_profiles INSERT를 한 트랜잭션으로 묶는다.
 * 둘 중 하나라도 실패하면 둘 다 롤백된다.
 * @param {{loginId:string, passwordHash:string, nickname:string}} u
 * @returns {Promise<number>} 신규 user id
 */
export async function insertUser({ loginId, passwordHash, nickname })
{
    const conn = await getPool().getConnection();
    try
    {
        await conn.beginTransaction();

        const [userRes] = await conn.execute(
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

/**
 * @param {number} userId
 * @returns {Promise<PlayerProfileRow|null>}
 */
export async function findProfileByUserId(userId)
{
    const [rows] = await getPool().execute(
        'SELECT user_id, score, level, exp, wins, losses, matches_played, last_match_at ' +
        'FROM player_profiles WHERE user_id = ? LIMIT 1',
        [userId]
    );
    return rows.length ? rows[0] : null;
}
