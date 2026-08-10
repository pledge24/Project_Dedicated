// 매치 결과 → 점수·경험치·레벨 파생 규칙(순수). DB 접근 없음.
// result.repository에서 분리한 이유: repository는 DB 쿼리만 담당한다(backend.md 4-레이어).
// 배점표를 바꿀 때 트랜잭션 코드를 읽을 필요가 없도록 정책만 여기 모은다.
import { config } from '../../common/config.js';

// 등수별 경험치(1~4등). placement 범위를 벗어나면 최저값.
const PLACEMENT_EXP = [100, 70, 40, 20];

// 등수별 기본 배점(1~4등). 1·2위 양수, 4위 음수 → 매치 총합이 순양수(인플레이션).
// 3위는 0이라 ELO항이 부호를 결정(소폭 +/−). 순수 ELO(elo.ts)에 이 값을 더해 clamp한다.
const PLACEMENT_BASE_SCORE_DELTAS = [45, 20, 0, -35];

// 레벨당 필요 경험치(전 구간 균일). level = floor(exp / EXP_PER_LEVEL) + 1.
const EXP_PER_LEVEL = 1000;

/** 완주자 한 명의 점수·레벨 파생값. floor·ceiling 적용 후 실제 변화량을 scoreDelta로 반환 → before+delta=after 보장. */
export function computePlayerResult(placement: number, before: number, delta: number, scoreFloor: number, scoreCeiling: number, expBefore: number)
{
    // delta는 순수 ELO항. 등수 기본배점을 더한 뒤 상·하한으로 clamp.
    const after = Math.min(scoreCeiling, Math.max(scoreFloor, before + delta + baseScoreDeltaForPlacement(placement)));
    const expGained = expForPlacement(placement);

    return {
        scoreBefore: before,
        scoreDelta: after - before,       // 반영된 실변화량(clamp 반영)
        scoreAfter: after,
        isWin: placement === 1 ? 1 : 0,
        expGained,
        levelAfter: levelForExp(expBefore + expGained),
    };
}

/** 탈주자 최하위 확정값 — base[꼴등] + 탈주 감점, clamp. 즉시 정산 전용. */
export function computeLeaverAfter(before: number, scoreFloor: number, scoreCeiling: number, leaverPenalty: number): number
{
    const lastPlacement = config.match.playersPerMatch;

    return Math.min(scoreCeiling, Math.max(scoreFloor, before + baseScoreDeltaForPlacement(lastPlacement) - leaverPenalty));
}

/** 등수별 획득 경험치. 범위를 벗어나면 최저값. */
function expForPlacement(placement: number): number
{
    return PLACEMENT_EXP[placement - 1] ?? PLACEMENT_EXP[PLACEMENT_EXP.length - 1];
}

/** 등수별 기본 배점. 범위를 벗어나면 최저값(꼴찌 취급). */
function baseScoreDeltaForPlacement(placement: number): number
{
    return PLACEMENT_BASE_SCORE_DELTAS[placement - 1] ?? PLACEMENT_BASE_SCORE_DELTAS[PLACEMENT_BASE_SCORE_DELTAS.length - 1];
}

/** 누적 경험치로 레벨 산출. 전 구간 EXP_PER_LEVEL당 1레벨 (Lv.1 = exp 0~999). */
function levelForExp(exp: number): number
{
    return Math.floor(exp / EXP_PER_LEVEL) + 1;
}
