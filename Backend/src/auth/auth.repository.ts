// 인증 도메인의 DB 쿼리만 담당 (repository 레이어)
import type { ResultSetHeader, RowDataPacket } from 'mysql2';

import { queryOne, withTransaction } from '../common/db.js';

/** DB users 행. */
export interface UserRow extends RowDataPacket
{
    id: number;
    login_id: string;
    password_hash: string;
    nickname: string;
}

/** token_version 단건 조회용 행. */
interface TokenVersionRow extends RowDataPacket
{
    token_version: number;
}

/** DB player_profiles 행. */
export interface PlayerProfileRow extends RowDataPacket
{
    user_id: number;
    score: number;
    level: number;
    exp: number;
    wins: number;
    losses: number;
    matches_played: number;
    last_match_at: Date | null;
}

export async function findByLoginId(loginId: string): Promise<UserRow | null>
{
    return queryOne<UserRow>(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE login_id = ? LIMIT 1',
        [loginId]
    );
}

export async function findByNickname(nickname: string): Promise<UserRow | null>
{
    return queryOne<UserRow>(
        'SELECT id, login_id, password_hash, nickname FROM users WHERE nickname = ? LIMIT 1',
        [nickname]
    );
}

/**
 * users INSERT + player_profiles INSERT를 한 트랜잭션으로 묶는다.
 * 둘 중 하나라도 실패하면 둘 다 롤백된다.
 */
export async function insertUser(
    { loginId, passwordHash, nickname }: { loginId: string; passwordHash: string; nickname: string }
): Promise<number>
{
    return withTransaction(async (conn) =>
    {
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

        return userId;
    });
}

export async function findProfileByUserId(userId: number): Promise<PlayerProfileRow | null>
{
    return queryOne<PlayerProfileRow>(
        'SELECT user_id, score, level, exp, wins, losses, matches_played, last_match_at ' +
        'FROM player_profiles WHERE user_id = ? LIMIT 1',
        [userId]
    );
}

/**
 * token_version을 +1 하고 새 값을 반환한다 (단일 세션 강제).
 * UPDATE→SELECT를 한 트랜잭션으로 묶어 동시 로그인 경합에도 실제 반영값을 돌려준다.
 */
export async function bumpTokenVersion(userId: number): Promise<number>
{
    return withTransaction(async (conn) =>
    {
        await conn.execute(
            'UPDATE users SET token_version = token_version + 1 WHERE id = ?',
            [userId]
        );
        const [rows] = await conn.execute<TokenVersionRow[]>(
            'SELECT token_version FROM users WHERE id = ? LIMIT 1',
            [userId]
        );

        return rows[0].token_version;
    });
}
