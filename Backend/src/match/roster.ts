// 매치 성사 시점의 진실(roster) — matchId로 결과 POST를 검증하기 위한 저장소.
// DB(match_rosters)가 진실 원천이고 메모리 Map은 그 write-through 캐시다. 기동 시 loadActive로 복원한다.
// 영속화하는 이유: 백엔드가 죽어도 확정된 DS는 살아남아 경기를 끝내므로(ds.shutdownUncommitted),
// 재시작한 백엔드가 그 DS의 serverToken을 검증할 수 있어야 결과가 유실되지 않는다.
// get()을 동기로 유지하는 것은 "프로세스 1개" 가정을 남겨둔 것 — 기동 시 전량 로드하므로 캐시 미스가 없다.
// 다중 프로세스로 나갈 때 여기만 async로 바꾸면 되고, 호출부(dsApi)는 그때 함께 옮긴다.
// 멱등성은 DB의 client_match_id UNIQUE로 일원화하므로 결과 확정 시 roster를 지우지 않는다.
// (지우면 재제출이 404가 되어 409와 의미가 갈림.) 대신 만료분만 register 시 청소한다.
import { config } from '../common/config.js';
import { logger } from '../common/logger.js';
import * as repo from './roster.repository.js';

/** 명단에 저장된 플레이어 데이터 단위 */
export interface RosterPlayer
{
    userId: number;
    nickname: string;
    joinToken: string;      // 매치별 1회용 입장 토큰. DS가 ?join= 으로 받은 토큰을 이 신원에 매핑. 봇은 빈 문자열.
    bot?: boolean;          // 봇전 봇 좌석(DB 미존재). true면 결과 저장 시 프로필/participants 기록 skip.
    rating?: number;        // 봇 ELO 입력 점수(백엔드 소유). 봇에만 존재.
}

/** 매치된 게임의 플레이어 명단(roster) */
export interface MatchRoster
{
    matchId: string;
    serverToken: string;
    mapName: string;
    startedAt: number;      // epoch ms
    players: RosterPlayer[];
    /** 이 매치를 실행 중인 DS 주소. 재입장 안내에 쓴다. 008 이전에 등록된 행은 undefined. */
    server?: { host: string; port: number };
}

/** 명단 모음 - 모든 매치의 명단이 이 곳에 저장된다(중요!) */
const rosters = new Map<string, MatchRoster>();

/**
 * 기동 시 복원 — 아직 만료되지 않은 명단을 메모리로 올린다.
 * 실패 시 throw해 기동을 막는다: roster 없이 뜨면 살아남은 DS의 결과 보고가 전부 404가 되고,
 * 그 사실이 15분 뒤 "결과가 없다"로만 드러나 원인 추적이 불가능해진다.
 */
export async function loadActive(): Promise<number>
{
    const restored = await repo.selectActive(Date.now() - config.match.ds.maxLifetimeMs);
    for (const r of restored)
    {
        rosters.set(r.matchId, r);
    }

    return restored.length;
}

/** 매치 성사 시 등록. 등록 때마다 sweep 실행(결과 미수신 누수 방지) */
export async function register(roster: MatchRoster): Promise<void>
{
    // DB 우선 — 실패 시 호출측이 매치를 버릴 수 있도록 메모리에 흔적을 남기지 않는다.
    await repo.insert(roster);
    sweep(Date.now());
    rosters.set(roster.matchId, roster);
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

export function size(): number
{
    return rosters.size;
}

/**
 * 만료된 roster(DS 수명 시각 초과)를 자료구조에서 제거.
 * DB 정리는 실패해도 캐시 정합성에 영향이 없고(다음 기동의 loadActive가 같은 기준으로 거른다)
 * register를 막아서도 안 되므로 대기하지 않고 로그만 남긴다.
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

    repo.deleteExpired(cutoff).catch((err) =>
    {
        logger.warn({ err }, '만료 roster DB 정리 실패 — 다음 sweep에서 재시도');
    });
}
