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
    joinToken: string;      // 매치별 1회용 입장 토큰. DS가 ?join= 으로 받은 토큰을 이 신원에 매핑.
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

/** userId가 속한 활성 매치의 matchId. 없으면 undefined. (게임중 kick 표시용 역조회) */
export function findMatchByUser(userId: number): string | undefined
{
    for (const [id, r] of rosters)
    {
        if (r.players.some((p) => p.userId === userId))
        {
            return id;
        }
    }

    return undefined;
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
