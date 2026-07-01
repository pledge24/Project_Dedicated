// 매칭 큐 — 순수 알고리즘. 트랜스포트(WS)와 비결합이라 단위 테스트가 쉽다.
// score 오름차순 정렬 유지 + joinedAt(동률 seq)로 선착순 우대.
import { AppError, Codes } from '../common/errors.js';

/** Queue 단위. ref는 불투명 핸들(WS 레이어의 소켓 등) */
export interface QueueEntry<Ref = unknown>
{
    userId: number;
    nickname: string;
    score: number;
    joinedAt: number;       // epoch ms
    seq: number;            // 입장 순서 (joinedAt 동률 tiebreak)
    ref: Ref;
}

/** 매치 단위(playersPerMatch 명). */
export interface MatchGroup<Ref = unknown>
{
    entries: QueueEntry<Ref>[];
}

/** 매치 Queue 파라미터(Config) */
export interface MatchQueueParams
{
    playersPerMatch: number;
    baseWindow: number;
    expandRate: number;     // 대기 1초당 윈도우 확장폭
    maxWindow: number;
}

/** 매치 Queue 클래스 */
export class MatchQueue<Ref = unknown>
{
    private entries: QueueEntry<Ref>[] = []; // score 오름차순 유지
    private readonly params: MatchQueueParams;
    private seqCounter = 0;

    constructor(params: MatchQueueParams)
    {
        this.params = params;
    }

    get size(): number
    {
        return this.entries.length;
    }

    /** 큐에서 제거(취소/연결 끊김). 제거됐으면 true. */
    dequeue(userId: number): boolean
    {
        const idx = this.entries.findIndex((e) => e.userId === userId);
        if (idx < 0)
        {
            return false;
        }
        this.entries.splice(idx, 1);

        return true;
    }

    /** userId당 1자리. 중복이면 AppError(ALREADY_IN_QUEUE). score 정렬 위치에 삽입. */
    enqueue(input: { userId: number; nickname: string; score: number; joinedAt: number; ref: Ref }): QueueEntry<Ref>
    {
        if (this.has(input.userId))
        {
            throw new AppError(Codes.ALREADY_IN_QUEUE, '이미 매칭 큐에 있습니다.');
        }

        const entry: QueueEntry<Ref> = { ...input, seq: this.seqCounter++ };
        const idx = this.lowerBound(entry.score);
        this.entries.splice(idx, 0, entry);

        return entry;
    }

    has(userId: number): boolean
    {
        return this.entries.some((e) => e.userId === userId);
    }

    /**
     * 1 사이클 매칭. now(ms)는 주입형 — 테스트 결정론을 위해 외부에서 시각을 넘긴다.
     *
     * [정책] 대기 오래된 유저를 "앵커"로 우선 시도하되, 앵커가 방을 못 채우면
     *        그 앵커만 건너뛰고(다음 앵커로) 계속한다. 앵커는 큐에 남으므로
     *        다음 사이클에도 우선권을 유지한다.
     *
     * [이유] 예전 방식(못 채우면 break)은 외딴 고티어 한 명이 큐 맨 앞에 박혀
     *        뒤 유저 전부를 막는 head-of-line blocking을 일으킨다. 우선권은
     *        "시도 순서에서 앞"이어야지 "내가 묶일 때까지 뒤는 대기"가 아니다.
     *
     * 매치된 유저는 즉시 지우지 않고 표시(matched)만 하고, 사이클 끝에 일괄 제거한다.
     */
    runCycle(now: number): MatchGroup<Ref>[]
    {
        const { playersPerMatch } = this.params;
        const matches: MatchGroup<Ref>[] = [];
        const matched = new Set<number>(); // 이번 사이클에 방에 편입된 userId

        // 대기 오래된 순으로 앵커를 순회한다. now가 고정이라 이 정렬은 사이클 내내 안정적.
        const anchors = [...this.entries].sort(byWait);

        for (const seed of anchors)
        {
            if (matched.has(seed.userId))
            {
                continue; // 이미 앞선 앵커의 방에 들어감
            }

            const window = this.windowFor(seed, now);
            const others = this.entries.filter(
                (e) =>
                    e.userId !== seed.userId &&
                    !matched.has(e.userId) &&
                    Math.abs(e.score - seed.score) <= window,
            );

            if (others.length < playersPerMatch - 1)
            {
                continue; // ★ break 아님 — 앵커만 건너뛰고 다음 앵커 시도(큐엔 그대로 남음)
            }

            // 앵커는 반드시 자기 방에 포함. 나머지 자리는 윈도우 내 최장 대기자로 채운다.
            others.sort(byWait);
            const chosen = [seed, ...others.slice(0, playersPerMatch - 1)];
            for (const e of chosen)
            {
                matched.add(e.userId);
            }
            matches.push({ entries: chosen });
        }

        // 사이클 끝에 일괄 제거 (표시 → 제거 분리로 순회 중 인덱스 흔들림 방지)
        if (matched.size > 0)
        {
            this.entries = this.entries.filter((e) => !matched.has(e.userId));
        }

        return matches;
    }

    /** 디버그/테스트용 스냅샷(읽기 전용). */
    snapshot(): ReadonlyArray<QueueEntry<Ref>>
    {
        return this.entries;
    }

    /** score 이상이 처음 나오는 위치(이진 탐색) — 정렬 삽입용. */
    private lowerBound(score: number): number
    {
        let lo = 0;
        let hi = this.entries.length;
        while (lo < hi)
        {
            const mid = (lo + hi) >> 1;
            if (this.entries[mid].score < score)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }

        return lo;
    }

    private windowFor(seed: QueueEntry<Ref>, now: number): number
    {
        const waitSec = Math.max(0, (now - seed.joinedAt) / 1000);

        return Math.min(this.params.baseWindow + waitSec * this.params.expandRate, this.params.maxWindow);
    }
}

/** 대기시간 비교: joinedAt 오름차순, 동률이면 seq 오름차순(먼저 들어온 쪽이 앞). */
function byWait<Ref>(a: QueueEntry<Ref>, b: QueueEntry<Ref>): number
{
    if (a.joinedAt !== b.joinedAt)
    {
        return a.joinedAt - b.joinedAt;
    }

    return a.seq - b.seq;
}
