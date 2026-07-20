// DS↔백엔드 이탈 채널의 인메모리 상태 — 매치별 (1) kick 대기열, (2) 즉시 정산된 탈주자.
// 백엔드는 DS에 push 채널이 없어 DS가 GET /kicks로 폴링해 회수하고, 회수 즉시 POST /leaver로
// 통지하면 그 자리에서 점수를 확정한다. roster·queue와 동일한 "단일 프로세스·인메모리" 가정.

/** 즉시 정산 결과 — 매치 종료 저장 시 중복 적용을 막고 참가자 row에 실을 값. */
export interface SettledLeaver
{
    scoreDelta: number;
    scoreAfter: number;
}

/** matchId → kick 대상 userId 집합. */
const pendingKicks = new Map<string, Set<number>>();

/** matchId → (userId → 정산값). */
const settled = new Map<string, Map<number, SettledLeaver>>();

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

/** userId를 정산 완료로 기록. */
export function markSettled(matchId: string, userId: number, info: SettledLeaver): void
{
    let map = settled.get(matchId);
    if (!map)
    {
        map = new Map<number, SettledLeaver>();
        settled.set(matchId, map);
    }
    map.set(userId, info);
}

/** userId의 정산값(없으면 undefined = 미정산). */
export function getSettled(matchId: string, userId: number): SettledLeaver | undefined
{
    return settled.get(matchId)?.get(userId);
}

/** 매치 종료 시 정리(kick 대기열 + 정산 기록). */
export function clear(matchId: string): void
{
    pendingKicks.delete(matchId);
    settled.delete(matchId);
}
