// DS↔백엔드 이탈 채널의 인메모리 상태 — 매치별 kick 대기열.
// 백엔드는 DS에 push 채널이 없어 DS가 GET /kicks로 폴링해 회수하고, 회수 즉시 POST /leaver로
// 통지하면 그 자리에서 점수를 확정한다. roster·queue와 동일한 "단일 프로세스·인메모리" 가정.
// (탈주 정산의 멱등·경합 안전은 DB 원장 match_leaver_settlements가 담당 — 여기 두지 않는다.)

/** matchId → kick 대상 userId 집합. */
const pendingKicks = new Map<string, Set<number>>();

/** matchId의 userId를 kick 대상으로 표시(재로그인=세션 대체 시). */
export function markKick(matchId: string, userId: number): void
{
    let set = pendingKicks.get(matchId);
    if (!set)
    {
        set = new Set<number>();
        pendingKicks.set(matchId, set);
    }
    set.add(userId);
}

/** matchId의 현재 kick 대상 목록. DS 폴링이 매번 전량 수신 후 로컬 dedup(멱등). */
export function listKicks(matchId: string): number[]
{
    const set = pendingKicks.get(matchId);

    return set ? [...set] : [];
}

/** 매치 종료 시 kick 대기열 정리. */
export function clear(matchId: string): void
{
    pendingKicks.delete(matchId);
}
