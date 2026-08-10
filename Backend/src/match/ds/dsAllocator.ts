// DS 확보 경계(seam) — "매치를 실행할 서버를 어디서 얻는가"를 이 인터페이스 하나로 추상화한다.
// ws.ts는 로컬 프로세스인지 stub인지 알 필요가 없다(이전엔 config.match.ds.enabled 분기가 6곳 흩어져 있었다).
// 확장 지점: 머신 여러 대로 나갈 때 Agones/GameLift 구현체를 여기에 추가하면 ws.ts는 그대로 둔다.
import { config } from '../../common/config.js';
import * as ds from './ds.js';
import * as dsPorts from './dsPorts.js';
import * as readiness from './readiness.js';

interface DsServer
{
    host: string;
    port: number;
}

interface DsRosterEntry
{
    joinToken: string;
    userId: number;
    nickname: string;
}

interface DsBotEntry
{
    userId: number;
    nickname: string;
}

export interface DsAllocator
{
    /** 매치를 실행할 서버 확보. 포트 고갈 등 복구 불가 시 throw. */
    allocate(matchId: string, serverToken: string, expectedPlayers: number, roster: DsRosterEntry[], bots?: DsBotEntry[]): Promise<DsServer>;
    /** 서버가 플레이어를 받을 준비될 때까지 대기. 타임아웃·부팅 실패 시 throw. */
    waitUntilReady(matchId: string): Promise<void>;
    /**
     * 매치 성사 확정 — 이 서버를 독립 워크로드로 승격시킨다(백엔드 종료가 죽이지 않음).
     * Agones의 SDK.Allocate()에 대응하는 경계: "플레이어가 붙었다"를 오케스트레이터에 알리는 지점.
     */
    commit(matchId: string): void;
    /**
     * 확보한 서버를 즉시 회수(확정 창에서 매치가 깨졌을 때).
     * 포트가 아니라 matchId로 지목한다 — 나머지 5개 메서드와 키를 맞춰야 원격 구현이 가능하다.
     * (Agones/GameLift에서는 포트가 인스턴스를 식별하지 않는다. 여러 인스턴스가 같은 포트를 쓴다.)
     */
    release(matchId: string): void;
    /** 프로세스 종료 시 정리 — 확정 전 서버만 회수하고 라이브 매치는 살려 둔다. */
    shutdownUncommitted(): void;
    /** 부팅 시 이전 실행이 남긴 잔재 점검. */
    reapOrphans(): Promise<void>;
}

/** 같은 머신에 D1Server.exe를 매치당 하나씩 띄우는 구현. */
const localAllocator: DsAllocator = {
    allocate: (matchId, serverToken, expectedPlayers, roster, bots) =>
        ds.allocate(matchId, serverToken, expectedPlayers, roster, bots),
    // 준비 판정은 DS의 POST /ready → readiness.signal. 상한은 config의 readyTimeoutMs.
    waitUntilReady: (matchId) => readiness.waitForReady(matchId, config.match.ds.readyTimeoutMs),
    commit: (matchId) => ds.commit(matchId),
    release: (matchId) => ds.release(matchId),
    shutdownUncommitted: () => ds.shutdownUncommitted(),
    reapOrphans: () => dsPorts.reapOrphans(),
};

/**
 * ds.enabled=false 경로 — 봇·매칭 알고리즘 테스트용으로 실제 프로세스를 띄우지 않는다.
 * 프로세스가 없으니 준비 대기는 즉시 통과여야 한다(readiness는 영원히 signal되지 않으므로 위임 금지).
 */
const stubAllocator: DsAllocator = {
    allocate: () => Promise.resolve(config.match.stubServer),
    waitUntilReady: () => Promise.resolve(),
    commit: () =>
    {
        // 프로세스가 없으니 수명을 분리할 대상도 없다.
    },
    release: () =>
    {
        // 띄운 프로세스가 없으니 회수할 것도 없다.
    },
    shutdownUncommitted: () =>
    {
        // 상동.
    },
    reapOrphans: () => Promise.resolve(),
};

export const allocator: DsAllocator = config.match.ds.enabled ? localAllocator : stubAllocator;
