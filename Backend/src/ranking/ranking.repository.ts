// 랭킹 도메인의 DB 쿼리만 담당 (repository 레이어)
import type { RowDataPacket } from 'mysql2';

import { getPool } from '../common/db.js';

/** DB player_profiles JOIN users 행 (랭킹 페이지 쿼리). */
export interface RankingRow extends RowDataPacket
{
    user_id: number;
    nickname: string;
    score: number;
    level: number;
    wins: number;
    losses: number;
    matches_played: number;
}

/** COUNT(*) 행. */
interface CountRow extends RowDataPacket
{
    total: number;
}

/**
 * score DESC, user_id ASC 정렬로 한 페이지 조회. idx_pp_score_desc 활용.
 * limit/offset은 핸들러에서 정수 검증·클램프된 값이라 SQL에 직접 보간한다.
 * (mysql2 prepared-stmt의 LIMIT ? 바인딩은 버전 편차가 있어 query()로 우회 —
 *  result.repository.ts의 동적 IN(...) 선례와 동일 전략. 주입 위험 0.)
 */
export async function findRankingPage(limit: number, offset: number): Promise<RankingRow[]>
{
    const [rows] = await getPool().query<RankingRow[]>(
        'SELECT pp.user_id, u.nickname, pp.score, pp.level, pp.wins, pp.losses, pp.matches_played ' +
        'FROM player_profiles AS pp ' +
        'JOIN users AS u ON u.id = pp.user_id ' +
        `ORDER BY pp.score DESC, pp.user_id ASC LIMIT ${limit} OFFSET ${offset}`
    );

    return rows;
}

/** 전체 프로필 수 (meta.total). 페이지 쿼리와 병렬로 돈다. */
export async function countProfiles(): Promise<number>
{
    const [rows] = await getPool().execute<CountRow[]>(
        'SELECT COUNT(*) AS total FROM player_profiles'
    );

    return Number(rows[0].total);
}
