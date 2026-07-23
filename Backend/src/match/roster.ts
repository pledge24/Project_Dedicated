// 매치 성사 시점의 진실(roster) — matchId로 결과 POST를 검증하기 위한 인메모리 저장소.
// 큐(service)·DS 풀(ds)과 같은 "프로세스 1개·인메모리" 가정. 영속화하지 않는다.
// 멱등성은 DB의 client_match_id UNIQUE로 일원화하므로 결과 확정 시 roster를 지우지 않는다.
// (지우면 재제출이 404가 되어 409와 의미가 갈림.) 대신 만료분만 register 시 청소한다.
import { config } from '../common/config.js';

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
}

/** 명단 모음 - 모든 매치의 명단이 이 곳에 저장된다(중요!) */
const rosters = new Map<string, MatchRoster>();

/** 매치 성사 시 등록. 등록 때마다 sweep 실행(결과 미수신 누수 방지) */
export function register(roster: MatchRoster): void
{
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
export function remove(matchId: string): void
{
    rosters.delete(matchId);
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

/** 만료된 roster(DS 수명 시각 초과)를 자료구조에서 제거. */
function sweep(now: number): void
{
    const maxAge = config.match.ds.maxLifetimeMs;
    for (const [id, r] of rosters)
    {
        if (now - r.startedAt > maxAge)
        {
            rosters.delete(id);
        }
    }
}
