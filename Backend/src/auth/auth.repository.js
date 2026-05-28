// 인증 도메인의 DB 쿼리만 담당 (repository 레이어)
import { getPool } from '../common/db.js';

/** @typedef {import('../common/types.js').UserRow} UserRow */

/**
 * @param {string} loginId
 * @returns {Promise<UserRow|null>}
 */
export async function findByLoginId(loginId)
{
    const [rows] = await getPool().execute(
        'SELECT id, login_id, password_hash, nickname, score FROM users WHERE login_id = ? LIMIT 1',
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
        'SELECT id, login_id, password_hash, nickname, score FROM users WHERE nickname = ? LIMIT 1',
        [nickname]
    );
    return rows.length ? rows[0] : null;
}

/**
 * @param {{loginId:string, passwordHash:string, nickname:string}} u
 * @returns {Promise<number>} insertId
 */
export async function insertUser({ loginId, passwordHash, nickname })
{
    const [result] = await getPool().execute(
        'INSERT INTO users (login_id, password_hash, nickname) VALUES (?, ?, ?)',
        [loginId, passwordHash, nickname]
    );
    return result.insertId;
}
