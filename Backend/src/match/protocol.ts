// 매칭 WebSocket 프로토콜 — 클라/서버 메시지 모양의 단일 출처.
// 클라(언리얼)는 다음 슬라이스에서 이 계약을 따라 구현한다.
import type { BackendError } from '../common/types.js';

/** 클라 → 서버 메시지. */
export type ClientMessage =
    | { type: 'queue:join' }
    | { type: 'queue:cancel' };

/** match:found data. joinToken은 수신자 본인 것(per-recipient). */
export interface MatchFoundData
{
    matchId: string;
    server: { host: string; port: number };
    /** 수신자 본인의 입장 토큰. DS에 ?join= 으로 제시 → 권위 신원 매핑. */
    joinToken: string;
}

// session:invalid — 더 최신 로그인이 세션을 대체함(단일 세션). 이 소켓은 곧 close(4001)된다.
export type ServerMessageType = 'queue:joined' | 'queue:left' | 'match:found' | 'error' | 'session:invalid';

/** 서버 → 클라 메시지. HTTP 봉투({ok,data,error})에 type 디스크리미네이터를 더한 모양. */
export interface ServerMessage<T = unknown>
{
    type: ServerMessageType;
    ok: boolean;
    data?: T;
    error?: BackendError;
}
