// 게임중(DS 접속) 강제 회수 대기열 — 매치별로 "이 userId를 kick하라"를 모아둔다.
// 백엔드는 DS에 push 채널이 없으므로, DS가 GET /api/match/:matchId/kicks로 폴링해 회수한다.
// roster·queue와 동일한 "단일 프로세스·인메모리" 가정(영속화하지 않음).
const pending = new Map<string, Set<number>>();

/** matchId의 userId를 kick 대상으로 표시. 재로그인(세션 대체) 시 호출. */
export function markKick(matchId: string, userId: number): void
{
    let set = pending.get(matchId);
    if (!set)
    {
        set = new Set<number>();
        pending.set(matchId, set);
    }
    set.add(userId);
}

/** matchId의 현재 kick 대상 목록. DS 폴링이 매번 전량을 받아 로컬 dedup한다(멱등). */
export function listKicks(matchId: string): number[]
{
    const set = pending.get(matchId);

    return set ? [...set] : [];
}

/** 매치 종료 시 정리. */
export function clear(matchId: string): void
{
    pending.delete(matchId);
}
