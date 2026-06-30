// 매칭 WebSocket 네트워크 레이어.
// 같은 http.Server를 공유(noServer) → upgrade 헤더에서 JWT 인증 후 handleUpgrade.
import { randomBytes, randomUUID } from 'node:crypto';
import type { Server as HttpServer, IncomingMessage } from 'node:http';
import type { Duplex } from 'node:stream';
import { WebSocket, WebSocketServer } from 'ws';
import type { RawData } from 'ws';

import { extractBearerToken } from '../common/bearer.js';
import { config } from '../common/config.js';
import { AppError, Codes } from '../common/errors.js';
import type { ErrorKind } from '../common/errors.js';
import * as jwtUtil from '../common/jwt.js';
import { logger } from '../common/logger.js';
import type { AuthedUser } from '../common/types.js';
import * as ds from './ds.js';
import * as service from './match.service.js';
import type { ClientMessage, ServerMessage, ServerMessageType } from './protocol.js';
import type { MatchGroup } from './queue.js';
import * as roster from './roster.js';

const WS_PATH = '/ws/match';

/** 인증된 소켓에 붙는 컨텍스트. */
interface AuthedWs extends WebSocket
{
    userId: number;
    nickname: string;
    isAlive: boolean;
}

/** http.Server에 매칭 WS를 붙이고 사이클/heartbeat를 기동. stop()으로 정리. */
export function attachMatchWebSocket(server: HttpServer): { stop: () => void }
{
    const wss = new WebSocketServer({ noServer: true });

    /** upgrade 이벤트(클라가 요청) 핸들 함수 추가 */
    server.on('upgrade', (req: IncomingMessage, socket: Duplex, head: Buffer) =>
    {
        const { pathname } = new URL(req.url ?? '', 'http://localhost');
        if (pathname !== WS_PATH)
        {
            socket.destroy();

            return;
        }

        /** socket error 이벤트에 핸들링 함수를 추가했다 삭제하는 이유는
         *  인증 도중 error 발생 시, 핸들링 해줄 함수가 없기 때문.
         *  그래서 임시용으로 추가했다 삭제하는 것.
        */
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
            // 해당 emit은 클라이언트를 향하지 않음. 바로 아래 있는 wss.on('connection')을 향한다.
            wss.emit('connection', ws, req, user);
        });
    });

    /** 연결 이벤트(이 서버가 요청) 핸들 함수 추가 */
    wss.on('connection', (ws: WebSocket, _req: IncomingMessage, user: AuthedUser) => onConnection(wss, ws, user));

    // 죽은 연결 감지 — heartbeat 주기마다 pong 못 받은 소켓은 terminate.
    const heartbeat = setInterval(() =>
    {
        for (const client of wss.clients)
        {
            const newSocket = client as AuthedWs;
            if (!newSocket.isAlive)
            {
                newSocket.terminate();
                continue;
            }
            newSocket.isAlive = false;
            newSocket.ping();
        }
    }, config.match.heartbeatMs);
    heartbeat.unref();

    // 매치 인터벌 타이머 설정.
    const cycle = setInterval(runMatchCycle, config.match.cycleMs);
    cycle.unref();

    logger.info({ path: WS_PATH, cycleMs: config.match.cycleMs }, '매칭 WebSocket 시작');

    return {
        stop()
        {
            clearInterval(heartbeat);
            clearInterval(cycle);
            ds.shutdownAll();
            for (const client of wss.clients)
            {
                client.terminate();
            }
            wss.close();
        },
    };
}

/** 업그레이드 핸드셰이크의 Authorization 헤더에서 토큰 검증. 실패 시 null. */
function authenticate(req: IncomingMessage): AuthedUser | null
{
    const token = extractBearerToken(req.headers.authorization);
    if (!token)
    {
        return null;
    }

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
    if (ws.readyState === WebSocket.OPEN)
    {
        ws.send(JSON.stringify(msg));
    }
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

    /** 메세지 처리 */
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
    const newSocket = ws as AuthedWs;
    newSocket.userId = user.userId;
    newSocket.nickname = user.nickname;
    newSocket.isAlive = true;

    // 같은 userId의 기존 소켓 정리(중복 탭/재연결) — 한 유저 한 자리 보장.
    for (const other of wss.clients)
    {
        const o = other as AuthedWs;
        if (o !== newSocket && o.userId === user.userId)
        {
            service.leave(user.userId);
            o.terminate();
        }
    }

    newSocket.on('pong', () =>
    {
        newSocket.isAlive = true;
    });

    newSocket.on('message', (raw) =>
    {
        void onMessage(newSocket, raw);
    });

    newSocket.on('close', () =>
    {
        const removed = service.leave(newSocket.userId);
        logger.info({ userId: newSocket.userId, removed }, 'WS 종료 — 큐에서 제거');
    });

    newSocket.on('error', (err) =>
    {
        logger.warn({ err, userId: newSocket.userId }, 'WS 에러')
    });

    logger.info({ userId: newSocket.userId }, 'WS 연결 수립');
}

/** 1초 사이클: 매칭 시도 → 성사 그룹마다 DS 할당+푸시(비동기). */
function runMatchCycle(): void
{
    // runMatching이 매칭 즉시 큐에서 제거하므로, 비동기 할당 중 재매칭 위험은 없다.
    const groups = service.runMatching(Date.now());
    for (const group of groups)
    {
        void handleMatch(group);
    }
}

/** 한 매치 처리: DS 할당(또는 stub) → match:found 푸시. 할당 실패 시 에러 푸시. */
async function handleMatch(group: MatchGroup<WebSocket>): Promise<void>
{
    const matchId = randomUUID();
    // DS 결과 보고 인증용 매치별 서버 토큰. 새로 spawn하는 DS에게 커맨드라인으로 넘겨준다.
    const serverToken = randomBytes(24).toString('base64url');
    const joinPlayers = group.entries.map((e) => ({
        ref: e.ref,
        userId: e.userId,
        nickname: e.nickname,
        joinToken: randomBytes(16).toString('base64url'),
    }));

    let server: { host: string; port: number };

    /** 매치를 실행할 DS를 Spawn */
    try
    {
        server = config.match.ds.enabled
            ? await ds.allocate(matchId, serverToken, group.entries.length,
                joinPlayers.map((p) => ({ joinToken: p.joinToken, userId: p.userId })))
            : config.match.stubServer;
    }
    catch (err)
    {
        logger.error({ err, matchId }, 'DS 할당 실패 — 매치 취소');
        for (const e of group.entries)
        {
            sendError(e.ref, 'error', Codes.INTERNAL_ERROR, '게임 서버 할당에 실패했습니다.');
        }

        return;
    }

    const { data } = service.buildMatchFound(group, matchId, server);

    // 결과 POST 검증용 roster 등록(matchId → 신원 + 서버 토큰 + 입장 토큰).
    roster.register({
        matchId,
        serverToken,
        mapName: config.match.ds.map,
        startedAt: Date.now(),
        players: joinPlayers.map((p) => ({
            userId: p.userId,
            nickname: p.nickname,
            joinToken: p.joinToken,
        })),
    });

    // 각 클라에 본인 입장 토큰만 실어 보낸다.
    for (const p of joinPlayers)
    {
        send(p.ref, { type: 'match:found', ok: true, data: { ...data, joinToken: p.joinToken } });
    }
    logger.info({ matchId, server, players: joinPlayers.map((p) => p.userId) }, '매치 성사');
}
