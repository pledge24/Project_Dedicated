// DS 확보 경계(seam) — "매치를 실행할 서버를 어디서 얻는가"를 이 인터페이스 하나로 추상화한다.
// ws.ts는 로컬 프로세스인지 stub인지 알 필요가 없다(이전엔 config.match.ds.enabled 분기가 6곳 흩어져 있었다).
// 확장 지점: 머신 여러 대로 나갈 때 Agones/GameLift 구현체를 여기에 추가하면 ws.ts는 그대로 둔다.
import { config } from '../common/config.js';
import * as ds from './ds.js';
import * as readiness from './readiness.js';

export interface DsServer
{
    host: string;
    port: number;
}

export interface DsRosterEntry
{
    joinToken: string;
    userId: number;
    nickname: string;
}

export interface DsBotEntry
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
    /** 확보한 서버를 즉시 회수(확정 창에서 매치가 깨졌을 때). */
    release(port: number): void;
    /** 프로세스 종료 시 확보한 서버 전부 정리. */
    shutdownAll(): void;
    /** 부팅 시 이전 실행이 남긴 잔재 점검. */
    reapOrphans(): Promise<void>;
}

/** 같은 머신에 D1Server.exe를 매치당 하나씩 띄우는 구현. */
const localAllocator: DsAllocator = {
    allocate: (matchId, serverToken, expectedPlayers, roster, bots) =>
        ds.allocate(matchId, serverToken, expectedPlayers, roster, bots),
    // 준비 판정은 DS의 POST /ready → readiness.signal. 상한은 config의 readyTimeoutMs.
    waitUntilReady: (matchId) => readiness.waitForReady(matchId, config.match.ds.readyTimeoutMs),
    release: (port) => ds.release(port),
    shutdownAll: () => ds.shutdownAll(),
    reapOrphans: () => ds.reapOrphans(),
};

/**
 * ds.enabled=false 경로 — 봇·매칭 알고리즘 테스트용으로 실제 프로세스를 띄우지 않는다.
 * 프로세스가 없으니 준비 대기는 즉시 통과여야 한다(readiness는 영원히 signal되지 않으므로 위임 금지).
 */
const stubAllocator: DsAllocator = {
    allocate: () => Promise.resolve(config.match.stubServer),
    waitUntilReady: () => Promise.resolve(),
    release: () =>
    {
        // 띄운 프로세스가 없으니 회수할 것도 없다.
    },
    shutdownAll: () =>
    {
        // 상동.
    },
    reapOrphans: () => Promise.resolve(),
};

export const allocator: DsAllocator = config.match.ds.enabled ? localAllocator : stubAllocator;
