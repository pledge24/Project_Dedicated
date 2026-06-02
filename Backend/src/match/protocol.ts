// 매칭 WebSocket 프로토콜 — 클라/서버 메시지 모양의 단일 출처.
// 클라(언리얼)는 다음 슬라이스에서 이 계약을 따라 구현한다.
import type { BackendError } from '../common/types.js';

/** 클라 → 서버 메시지. */
export type ClientMessage =
    | { type: 'queue:join' }
    | { type: 'queue:cancel' };

/** match:found data의 플레이어 1명. slotIndex는 0~(N-1). */
export interface MatchPlayer
{
    userId: number;
    nickname: string;
    score: number;
    slotIndex: number;
}

/** match:found data. server 주소는 이번 슬라이스에서 stub. */
export interface MatchFoundData
{
    matchId: string;
    server: { host: string; port: number };
    players: MatchPlayer[];
}

export type ServerMessageType = 'queue:joined' | 'queue:left' | 'match:found' | 'error';

/** 서버 → 클라 메시지. HTTP 봉투({ok,data,error})에 type 디스크리미네이터를 더한 모양. */
export interface ServerMessage<T = unknown>
{
    type: ServerMessageType;
    ok: boolean;
    data?: T;
    error?: BackendError;
}
