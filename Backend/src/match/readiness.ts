// DS 준비 완료(ready) 콜백 gate — matchId로 "DS가 플레이어를 받을 준비됨" 신호를 대기한다.
// 큐(service)·roster·DS 풀(ds)과 같은 "프로세스 1개·인메모리" 가정. 영속화하지 않는다.
// handleMatch가 waitForReady로 대기 → DS의 POST /ready가 signal로 resolve, 부팅 크래시는 ds가 fail로 reject.
interface ReadyGate
{
    resolve: () => void;
    reject: (err: Error) => void;
    timer: NodeJS.Timeout;
}

const gates = new Map<string, ReadyGate>(); // matchId → gate

/** DS 준비 콜백을 timeoutMs까지 대기. 콜백(signal)이 오면 resolve, 타임아웃/조기실패(fail)면 reject. */
export function waitForReady(matchId: string, timeoutMs: number): Promise<void>
{
    return new Promise((resolve, reject) =>
    {
        const timer = setTimeout(() =>
        {
            gates.delete(matchId);
            reject(new Error(`DS ready 타임아웃 (matchId=${matchId}, ${timeoutMs}ms)`));
        }, timeoutMs);
        // pending gate가 graceful shutdown 때 이벤트 루프를 붙잡지 않게(ds.ts killTimer와 동일).
        if (typeof timer.unref === 'function')
        {
            timer.unref();
        }

        gates.set(matchId, { resolve, reject, timer });
    });
}

/** DS 통지(POST /ready): 대기 중인 gate를 resolve. 없으면 no-op(확정 후 재전송·지난 매치 방어). */
export function signal(matchId: string): void
{
    const gate = gates.get(matchId);
    if (!gate)
    {
        return;
    }

    clearTimeout(gate.timer);
    gates.delete(matchId);
    gate.resolve();
}

/** DS 프로세스 조기 사망: 대기 중인 gate를 즉시 reject(타임아웃 안 기다림). 없으면 no-op. */
export function fail(matchId: string, reason: string): void
{
    const gate = gates.get(matchId);
    if (!gate)
    {
        return;
    }

    clearTimeout(gate.timer);
    gates.delete(matchId);
    gate.reject(new Error(reason));
}
