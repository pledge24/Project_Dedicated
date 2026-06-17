// 매치 성사 시점의 진실(roster) — matchId로 결과 POST를 검증하기 위한 인메모리 저장소.
// 큐(service)·DS 풀(ds)과 같은 "프로세스 1개·인메모리" 가정. 영속화하지 않는다.
// 멱등성은 DB의 client_match_id UNIQUE로 일원화하므로 결과 확정 시 roster를 지우지 않는다.
// (지우면 재제출이 404가 되어 409와 의미가 갈림.) 대신 만료분만 register 시 청소한다.
import { config } from '../common/config.js';

export interface RosterPlayer
{
    userId: number;
    nickname: string;
    /** 매치별 1회용 입장 토큰. DS가 ?join= 으로 받은 토큰을 이 신원에 매핑. */
    joinToken: string;
}

export interface MatchRoster
{
    matchId: string;
    serverToken: string;
    mapName: string;
    startedAt: number;   // epoch ms
    players: RosterPlayer[];
}

const rosters = new Map<string, MatchRoster>();

/** 매치 성사 시 등록. 등록 때마다 만료(maxLifetime 초과) roster를 청소한다(결과 미수신 누수 방지). */
export function register(roster: MatchRoster): void
{
    sweep(Date.now());
    rosters.set(roster.matchId, roster);
}

export function get(matchId: string): MatchRoster | undefined
{
    return rosters.get(matchId);
}

export function size(): number
{
    return rosters.size;
}

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
