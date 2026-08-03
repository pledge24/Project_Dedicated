// 랭킹 비즈니스 로직 (service 레이어). 위치 순위 부여 + 응답 조립 (행→카멜 변환은 repository 소관).
import type { RankingEntry, RankingResponse } from '../common/types.js';
import * as repo from './ranking.repository.js';

/**
 * 한 페이지 + 전체 수 + 요청자 전역 순위를 병렬 조회해 RankingResponse로 조립.
 * entries[].rank = offset + index + 1 (정렬 순서상 위치). me.rank = 총순서 기준 유일 순위(페이지 무관, 리스트 위치와 일치).
 * 빈 페이지도 정상(entries: []).
 */
export async function fetchRanking(userId: number, limit: number, offset: number): Promise<RankingResponse>
{
    const [profiles, total, rank] = await Promise.all([
        repo.listRankingPage(limit, offset),
        repo.countProfiles(),
        repo.findRankByUserId(userId),
    ]);

    const entries: RankingEntry[] = profiles.map((profile, i) => ({ rank: offset + i + 1, ...profile }));

    return { entries, me: { rank }, meta: { total, limit, offset } };
}
