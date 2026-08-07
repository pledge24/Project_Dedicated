// 매칭 WS 전송 헬퍼 — network(ws.ts)와 service(matchFormation.service.ts)가 함께 쓴다.
// 별도 모듈인 이유: 양쪽이 서로를 import하면 순환이 된다. 전송 자체는 어느 레이어에도 속하지 않는 배관이다.
import { WebSocket } from 'ws';

import type { ErrorKind } from '../common/errors.js';
import type { ServerMessage, ServerMessageType } from './protocol.types.js';

export function send(ws: WebSocket, msg: ServerMessage): void
{
    if (ws.readyState === WebSocket.OPEN)
    {
        ws.send(JSON.stringify(msg));
    }
}

export function sendError(ws: WebSocket, type: ServerMessageType, kind: ErrorKind, message: string): void
{
    send(ws, { type, ok: false, error: { code: kind.code, message } });
}
