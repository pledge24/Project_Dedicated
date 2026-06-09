// 매칭 큐 — 순수 알고리즘. 트랜스포트(WS)와 비결합이라 단위 테스트가 쉽다.
// score 오름차순 정렬 유지 + joinedAt(동률 seq)로 선착순 우대.
import { AppError, Codes } from '../common/errors.js';

/** 큐의 한 자리. ref는 불투명 핸들(WS 레이어의 소켓 등) — 알고리즘은 안 읽는다. */
export interface QueueEntry<Ref = unknown>
{
    userId: number;
    nickname: string;
    score: number;
    joinedAt: number; // epoch ms
    seq: number;       // 입장 순서 (joinedAt 동률 tiebreak)
    ref: Ref;
}

/** 성사된 한 매치(playersPerMatch 명). */
export interface MatchGroup<Ref = unknown>
{
    entries: QueueEntry<Ref>[];
}

export interface MatchQueueParams
{
    playersPerMatch: number;
    baseWindow: number;
    expandRate: number; // 대기 1초당 윈도우 확장폭
    maxWindow: number;
}

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
     * 절차: seed=최장대기 → seed 윈도우 안 후보 ≥N이면 그중 최장대기 N명 매치.
     *       같은 tick에서 더 못 만들 때까지 반복(여러 매치 가능).
     * seed가 매치를 못 만들면 break — 선착순(최장대기 우선)을 깨지 않는다.
     */
    runCycle(now: number): MatchGroup<Ref>[]
    {
        const { playersPerMatch } = this.params;
        const matches: MatchGroup<Ref>[] = [];

        while (this.entries.length >= playersPerMatch)
        {
            const seed = this.pickSeed();
            const window = this.windowFor(seed, now);
            const candidates = this.entries.filter((e) => Math.abs(e.score - seed.score) <= window);
            if (candidates.length < playersPerMatch)
            {
                break;
            }

            candidates.sort(byWait);
            const chosen = candidates.slice(0, playersPerMatch);
            const chosenIds = new Set(chosen.map((e) => e.userId));
            this.entries = this.entries.filter((e) => !chosenIds.has(e.userId));
            matches.push({ entries: chosen });
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

    /** 최장대기 자리 = byWait 최소. */
    private pickSeed(): QueueEntry<Ref>
    {
        let seed = this.entries[0];
        for (const e of this.entries)
        {
            if (byWait(e, seed) < 0)
            {
                seed = e;
            }
        }

        return seed;
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
