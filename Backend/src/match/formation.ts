// 매치 확정(formation) 창의 순수 결정 로직 — WS/DB 비의존이라 단위 테스트가 쉽다.

/**
 * 확정 중단 시 큐로 되돌릴 대상 선별.
 * 조건: 아직 formation 유효(inFormation) + 소켓 열림 + 큐에 미존재.
 * 끊김/재접속/취소는 전부 호출부의 leave가 inFormation을 지우므로 isInFormation=false로 걸러진다.
 * ref 타입은 entries에서 추론(E['ref']) — WS 소켓이든 테스트 stub이든 무관.
 */
export function selectRequeue<E extends { userId: number; ref: unknown }>(
    entries: readonly E[],
    preds: {
        isInFormation: (userId: number) => boolean;
        isOpen: (ref: E['ref']) => boolean;
        isQueued: (userId: number) => boolean;
    },
): E[]
{
    return entries.filter((e) =>
        preds.isInFormation(e.userId) && preds.isOpen(e.ref) && !preds.isQueued(e.userId));
}
