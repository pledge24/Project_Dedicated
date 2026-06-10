// 랭킹 비즈니스 로직 (service 레이어). 행 → DTO 매핑 + 위치 순위 부여.
import type { RankingEntry, RankingResponse } from '../common/types.js';
import * as repo from './ranking.repository.js';

/**
 * 한 페이지 + 전체 수를 조회해 RankingResponse로 조립.
 * rank = offset + index + 1 (정렬 순서상 위치). 빈 페이지도 정상(entries: []).
 */
export async function getRanking(limit: number, offset: number): Promise<RankingResponse>
{
    const [rows, total] = await Promise.all([
        repo.findRankingPage(limit, offset),
        repo.countProfiles(),
    ]);

    const entries: RankingEntry[] = rows.map((row, i) => ({
        rank: offset + i + 1,
        userId: row.user_id,
        nickname: row.nickname,
        score: row.score,
        level: row.level,
        wins: row.wins,
        losses: row.losses,
        matchesPlayed: row.matches_played,
    }));

    return { entries, meta: { total, limit, offset } };
}
