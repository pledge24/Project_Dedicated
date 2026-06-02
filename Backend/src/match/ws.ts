// 매칭 WebSocket 네트워크 레이어.
// 같은 http.Server를 공유(noServer) → upgrade 헤더에서 JWT 인증 후 handleUpgrade.
import type { IncomingMessage, Server as HttpServer } from 'node:http';
import type { Duplex } from 'node:stream';

import { WebSocketServer, WebSocket } from 'ws';
import type { RawData } from 'ws';

import { config } from '../common/config.js';
import { AppError, Codes } from '../common/errors.js';
import type { ErrorKind } from '../common/errors.js';
import * as jwtUtil from '../common/jwt.js';
import { logger } from '../common/logger.js';
import type { AuthedUser } from '../common/types.js';
import * as service from './service.js';
import type { ClientMessage, ServerMessage, ServerMessageType } from './protocol.js';

const WS_PATH = '/ws/match';
const BEARER_PREFIX = 'Bearer ';

/** 인증된 소켓에 붙는 컨텍스트. */
interface AuthedWs extends WebSocket
{
    userId: number;
    nickname: string;
    isAlive: boolean;
}

/** 업그레이드 핸드셰이크의 Authorization 헤더에서 토큰 검증. 실패 시 null. */
function authenticate(req: IncomingMessage): AuthedUser | null
{
    const header = req.headers.authorization;
    if (!header || !header.startsWith(BEARER_PREFIX)) return null;

    const token = header.slice(BEARER_PREFIX.length).trim();
    if (!token) return null;

    try
    {
        const claims = jwtUtil.verify(token);
        return { userId: claims.userId, nickname: claims.nickname };
    }
    catch
    {
        return null;
    }
}

function onSocketError(err: Error): void
{
    logger.warn({ err }, 'WS 업그레이드 소켓 에러');
}

function send(ws: WebSocket, msg: ServerMessage): void
{
    if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
}

function sendError(ws: WebSocket, type: ServerMessageType, kind: ErrorKind, message: string): void
{
    send(ws, { type, ok: false, error: { code: kind.code, message } });
}

/** WS 메시지 처리 — queue:join / queue:cancel. */
async function onMessage(ws: AuthedWs, raw: RawData): Promise<void>
{
    let msg: ClientMessage;
    try
    {
        msg = JSON.parse(raw.toString()) as ClientMessage;
    }
    catch
    {
        sendError(ws, 'error', Codes.BAD_MESSAGE, '메시지 JSON 파싱 실패');
        return;
    }

    try
    {
        switch (msg.type)
        {
            case 'queue:join':
                await service.join(ws.userId, ws);
                send(ws, { type: 'queue:joined', ok: true, data: { userId: ws.userId } });
                logger.info({ userId: ws.userId, size: service.queueSize() }, '큐 입장');
                break;

            case 'queue:cancel':
            {
                const removed = service.leave(ws.userId);
                send(ws, { type: 'queue:left', ok: true, data: { userId: ws.userId, removed } });
                logger.info({ userId: ws.userId, removed }, '큐 취소');
                break;
            }

            default:
                sendError(ws, 'error', Codes.BAD_MESSAGE, '알 수 없는 메시지 type');
        }
    }
    catch (err)
    {
        if (err instanceof AppError)
        {
            sendError(ws, 'error', err.kind, err.message);
            return;
        }
        logger.error({ err, userId: ws.userId }, '매칭 메시지 처리 실패');
        sendError(ws, 'error', Codes.INTERNAL_ERROR, '서버 오류가 발생했습니다.');
    }
}

function onConnection(wss: WebSocketServer, ws: WebSocket, user: AuthedUser): void
{
    const aws = ws as AuthedWs;
    aws.userId = user.userId;
    aws.nickname = user.nickname;
    aws.isAlive = true;

    // 같은 userId의 기존 소켓 정리(중복 탭/재연결) — 한 유저 한 자리 보장.
    for (const other of wss.clients)
    {
        const o = other as AuthedWs;
        if (o !== aws && o.userId === user.userId)
        {
            service.leave(user.userId);
            o.terminate();
        }
    }

    aws.on('pong', () => { aws.isAlive = true; });
    aws.on('message', (raw) => { void onMessage(aws, raw); });
    aws.on('close', () =>
    {
        const removed = service.leave(aws.userId);
        logger.info({ userId: aws.userId, removed }, 'WS 종료 — 큐에서 제거');
    });
    aws.on('error', (err) => logger.warn({ err, userId: aws.userId }, 'WS 에러'));

    logger.info({ userId: aws.userId }, 'WS 연결 수립');
}

/** 1초 사이클: 매칭 시도 → 성사분 4명 소켓에 match:found 푸시. */
function runMatchCycle(): void
{
    const groups = service.runMatching(Date.now());
    for (const group of groups)
    {
        const { data, targets } = service.buildMatchFound(group);
        for (const ref of targets)
        {
            send(ref, { type: 'match:found', ok: true, data });
        }
        logger.info({ matchId: data.matchId, players: data.players.map((p) => p.userId) }, '매치 성사');
    }
}

/** http.Server에 매칭 WS를 붙이고 사이클/heartbeat를 기동. stop()으로 정리. */
export function attachMatchWebSocket(server: HttpServer): { stop: () => void }
{
    const wss = new WebSocketServer({ noServer: true });

    server.on('upgrade', (req: IncomingMessage, socket: Duplex, head: Buffer) =>
    {
        const { pathname } = new URL(req.url ?? '', 'http://localhost');
        if (pathname !== WS_PATH)
        {
            socket.destroy();
            return;
        }

        socket.on('error', onSocketError);

        const user = authenticate(req);
        if (!user)
        {
            socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
            socket.destroy();
            return;
        }

        socket.removeListener('error', onSocketError);
        wss.handleUpgrade(req, socket, head, (ws) =>
        {
            wss.emit('connection', ws, req, user);
        });
    });

    wss.on('connection', (ws: WebSocket, _req: IncomingMessage, user: AuthedUser) => onConnection(wss, ws, user));

    // 죽은 연결 감지 — heartbeat 주기마다 pong 못 받은 소켓은 terminate.
    const heartbeat = setInterval(() =>
    {
        for (const client of wss.clients)
        {
            const aws = client as AuthedWs;
            if (!aws.isAlive)
            {
                aws.terminate();
                continue;
            }
            aws.isAlive = false;
            aws.ping();
        }
    }, config.match.heartbeatMs);
    heartbeat.unref();

    const cycle = setInterval(runMatchCycle, config.match.cycleMs);
    cycle.unref();

    logger.info({ path: WS_PATH, cycleMs: config.match.cycleMs }, '매칭 WebSocket 시작');

    return {
        stop()
        {
            clearInterval(heartbeat);
            clearInterval(cycle);
            for (const client of wss.clients) client.terminate();
            wss.close();
        },
    };
}
