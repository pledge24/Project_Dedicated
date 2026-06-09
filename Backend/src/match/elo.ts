// ELO 점수 계산 — 4인 FFA를 "각자 vs 나머지 전원"의 쌍비교로 환산(순수 함수, DB 의존 없음).
// 표준 ELO: E = 1/(1+10^((Rj-Ri)/400)), 변화 = K(S-E). FFA는 모든 쌍의 변화를 합산 후 (N-1)로 정규화.
// 모든 쌍의 (S-E) 합이 0이므로 deltas의 합은 0(반올림 오차 ±N) — 레이팅 총량 보존.

/**
 * @param ratings    각 플레이어의 현재 점수
 * @param placements 각 플레이어의 등수(1=최고, 동점 허용). ratings와 인덱스 정렬.
 * @param k          K-factor
 * @returns 각 플레이어의 점수 변화(정수). ratings와 같은 순서.
 */
export function computeFfaEloDeltas(ratings: number[], placements: number[], k: number): number[]
{
    const n = ratings.length;
    if (n !== placements.length)
    {
        throw new Error('ratings와 placements 길이가 다릅니다.');
    }
    if (n < 2)
    {
        return new Array(n).fill(0);
    }

    const deltas: number[] = [];
    for (let i = 0; i < n; i++)
    {
        let sum = 0;
        for (let j = 0; j < n; j++)
        {
            if (i === j)
            {
                continue;
            }

            const expected = 1 / (1 + 10 ** ((ratings[j] - ratings[i]) / 400));
            const actual = placements[i] < placements[j] ? 1
                : placements[i] > placements[j] ? 0
                    : 0.5;
            sum += actual - expected;
        }

        deltas.push(Math.round((k / (n - 1)) * sum));
    }

    return deltas;
}
