// 매치 성사 시점의 진실(roster) — matchId로 결과 POST를 검증하기 위한 저장소.
// DB(match_rosters)가 진실 원천이고 메모리 Map은 그 write-through 캐시다. 기동 시 fetchActive로 복원한다.
// 영속화하는 이유: 백엔드가 죽어도 확정된 DS는 살아남아 경기를 끝내므로(ds.shutdownUncommitted),
// 재시작한 백엔드가 그 DS의 serverToken을 검증할 수 있어야 결과가 유실되지 않는다.
// get()을 동기로 유지하는 것은 "프로세스 1개" 가정을 남겨둔 것 — 기동 시 전량 로드하므로 캐시 미스가 없다.
// 다중 프로세스로 나갈 때 여기만 async로 바꾸면 되고, 호출부(dsApi)는 그때 함께 옮긴다.
// 멱등성은 DB의 client_match_id UNIQUE로 일원화하므로 결과 확정 시 roster를 지우지 않는다.
// (지우면 재제출이 404가 되어 409와 의미가 갈림.) 대신 만료분만 add 시 청소한다.
import { config } from '../../common/config.js';
import { logger } from '../../common/logger.js';
import * as resultRepo from '../result/result.repository.js';
import * as repo from './roster.repository.js';
import type { MatchRoster } from './roster.types.js';

export type { MatchRoster, RosterPlayer } from './roster.types.js';

/** 명단 모음 - 모든 매치의 명단이 이 곳에 저장된다(중요!) */
const rosters = new Map<string, MatchRoster>();

/**
 * 기동 시 복원 — 아직 만료되지 않은 명단을 메모리로 올린다.
 * 실패 시 throw해 기동을 막는다: roster 없이 뜨면 살아남은 DS의 결과 보고가 전부 404가 되고,
 * 그 사실이 15분 뒤 "결과가 없다"로만 드러나 원인 추적이 불가능해진다.
 */
export async function fetchActive(): Promise<number>
{
    const restored = await repo.listActive(Date.now() - config.match.ds.maxLifetimeMs);
    for (const r of restored)
    {
        rosters.set(r.matchId, r);
    }

    return restored.length;
}

/** 매치 성사 시 등록. 등록 때마다 sweep 실행(결과 미수신 누수 방지) */
export async function add(roster: MatchRoster): Promise<void>
{
    // DB 우선 — 실패 시 호출측이 매치를 버릴 수 있도록 메모리에 흔적을 남기지 않는다.
    await repo.insert(roster);
    sweep(Date.now());
    rosters.set(roster.matchId, roster);
}

/**
 * DS가 시작 게이트를 통과했음을 기록 — 이 시각이 찍히면 findRejoinableMatch가 주소를 주지 않는다.
 * 첫 통지만 반영한다: DS는 유실 대비로 재전송하고, 그때마다 덮어쓰면 시각이 뒤로 밀린다.
 * DB 우선(add와 같은 순서) — 영속화가 실패했는데 메모리만 시작으로 바뀌면 재시작 후 재입장 창이 되열린다.
 */
export async function markPlayStarted(matchId: string, atEpochMs: number): Promise<void>
{
    const roster = rosters.get(matchId);
    if (!roster || roster.playStartedAt !== undefined)
    {
        return;
    }

    await repo.updatePlayStarted(matchId, atEpochMs);
    roster.playStartedAt = atEpochMs;
}

export function get(matchId: string): MatchRoster | undefined
{
    return rosters.get(matchId);
}

/**
 * 매치를 명단에서 즉시 제거. 준비 대기 중 취소(DS 준비 실패·확정창 이탈)로 미확정 매치를 버릴 때 사용.
 * (확정된 매치는 결과 재제출 멱등 위해 sweep 전까지 유지 — 이 함수로 지우지 않는다.)
 */
export async function remove(matchId: string): Promise<void>
{
    rosters.delete(matchId);
    await repo.deleteById(matchId);
}

/**
 * userId가 속한 활성 매치의 matchId. 없으면 undefined. (게임중 kick 역조회)
 * roster는 결과 재제출 멱등 위해 종료 후에도 남으므로(sweep 전까지), 삽입순 첫 매치를
 * 쓰면 스테일(끝난) 매치가 잡혀 kick이 죽은 매치로 간다. 단일세션이라 유저의 활성 매치는
 * ≤1개 → 가장 최근(startedAt 최대) 성사분이 곧 현재 매치.
 */
export function findMatchByUser(userId: number): string | undefined
{
    let latestId: string | undefined;
    let latestStartedAt = -1;
    for (const [id, r] of rosters)
    {
        if (r.startedAt > latestStartedAt && r.players.some((p) => p.userId === userId))
        {
            latestId = id;
            latestStartedAt = r.startedAt;
        }
    }

    return latestId;
}

/**
 * 만료된 roster(DS 수명 시각 초과)를 자료구조에서 제거.
 * DB 쪽 처리는 실패해도 캐시 정합성에 영향이 없고(다음 기동의 fetchActive가 같은 기준으로 거른다)
 * add를 막아서도 안 되므로 대기하지 않고 로그만 남긴다.
 */
function sweep(now: number): void
{
    const cutoff = now - config.match.ds.maxLifetimeMs;
    for (const [id, r] of rosters)
    {
        if (r.startedAt <= cutoff)
        {
            rosters.delete(id);
        }
    }

    settleExpired(now).catch((err) =>
    {
        logger.warn({ err }, '만료 roster 정리 실패 — 다음 sweep에서 재시도');
    });
}

/**
 * 만료된 매치를 DB에서 정리하고, 결과가 끝내 오지 않은 것을 abort로 기록한다.
 *
 * 이 경로가 필요한 이유: 확정된 DS는 백엔드 수명과 분리돼 있어(ds.commit) 게임 도중 죽어도
 * 아무도 그 사실을 모른다. readiness gate는 이미 소비돼 no-op이고, roster는 조용히 삭제될 뿐이라
 * 매치가 시작된 흔적조차 남지 않았다. 여기서 최소한 "시작됐고 결과가 없다"를 남긴다.
 *
 * 삭제를 먼저 하는 것이 중요하다 — roster가 사라지면 늦게 도착한 결과 POST는 serverToken 검증에서
 * 404로 걸러진다. 반대로 abort를 먼저 쓰면 client_match_id UNIQUE에 걸려 진짜 결과가 409로 버려진다.
 */
export async function settleExpired(now: number = Date.now()): Promise<number>
{
    const cutoff = now - config.match.ds.maxLifetimeMs;
    const expired = await repo.listExpired(cutoff);
    if (expired.length === 0)
    {
        return 0;
    }

    // 인증의 진실 원천은 메모리 캐시다 — DB만 지우면 늦은 결과가 여전히 통과한다.
    // sweep 경로는 이미 지웠지만 부팅·직접 호출 경로도 있으므로 여기서 한 번 더(멱등).
    for (const m of expired)
    {
        rosters.delete(m.matchId);
    }

    await repo.deleteExpired(cutoff);

    let recorded = 0;
    for (const m of expired)
    {
        if (await resultRepo.hasResult(m.matchId))
        {
            continue;
        }

        if (await resultRepo.recordAbortedMatch(m.matchId, m.mapName, new Date(m.startedAt), new Date(now)))
        {
            recorded += 1;
            logger.warn({ matchId: m.matchId, startedAt: new Date(m.startedAt).toISOString() },
                '결과 미보고 매치 — abort로 기록(DS 크래시 추정, 점수 변동 없음)');
        }
    }

    return recorded;
}
