// 매칭 WebSocket 네트워크 레이어 — 소켓 수명만 다룬다.
// 같은 http.Server를 공유(noServer) → upgrade 헤더에서 JWT 인증 후 handleUpgrade.
// 매치가 성사된 뒤의 조립(토큰 발급·DS 할당·roster 등록)은 matchFormation.handler가 맡는다.
import type { Server as HttpServer, IncomingMessage } from 'node:http';
import type { Duplex } from 'node:stream';
import { WebSocket, WebSocketServer } from 'ws';
import type { RawData } from 'ws';

import { extractBearerToken } from '../common/bearer.js';
import { config } from '../common/config.js';
import { AppError, Codes } from '../common/errors.js';
import * as jwtUtil from '../common/jwt.js';
import { logger } from '../common/logger.js';
import { getCurrentTokenVersion, onSuperseded } from '../common/session.js';
import type { AuthedUser } from '../common/types.js';
import { allocator } from './dsAllocator.js';
import { handleBotMatch, handleMatch } from './matchFormation.handler.js';
import * as service from './matchmaking.service.js';
import type { ClientMessage } from './protocol.js';
import { send, sendError } from './wsSend.js';

const WS_PATH = '/ws/match';

/** 인증된 소켓에 붙는 컨텍스트. */
interface AuthedWs extends WebSocket
{
    userId: number;
    nickname: string;
    isAlive: boolean;
    msgWindowStart: number;  // rate limit 고정 윈도우 시작 시각(epoch ms)
    msgCount: number;        // 현재 윈도우의 수신 메시지 수
    limitNotified: boolean;  // 이번 윈도우에 초과 경고를 이미 보냈는가
}

/** http.Server에 매칭 WS를 붙이고 사이클/heartbeat를 기동. stop()으로 정리. */
export function attachMatchWebSocket(server: HttpServer): { stop: () => void }
{
    // maxPayload: ws 기본값 100MiB → 16KB. 큐 메시지는 수십 바이트라 대형 메시지 flood 차단.
    const wss = new WebSocketServer({ noServer: true, maxPayload: 16 * 1024 });

    /** upgrade 이벤트(클라가 요청) 핸들 함수 추가 */
    server.on('upgrade', (req: IncomingMessage, socket: Duplex, head: Buffer) =>
    {
        // async 리스너를 그대로 등록하면 reject를 아무도 잡지 않아 unhandledRejection → 프로세스 종료다.
        // 핸드셰이크 1건의 실패는 그 소켓만 버리고 끝나야 한다.
        handleUpgrade(wss, req, socket, head).catch((err) =>
        {
            logger.error({ err }, 'WS 업그레이드 처리 실패 — 소켓 종료');
            socket.destroy();
        });
    });

    /** 연결 이벤트(이 서버가 요청) 핸들 함수 추가 */
    wss.on('connection', (ws: WebSocket, _req: IncomingMessage, user: AuthedUser) => onConnection(wss, ws, user));

    // 재로그인(세션 대체) 알림 구독 — 버전 대조는 핸드셰이크 때뿐이라 이미 연결된 옛 소켓은 여기서 끊는다.
    // 게임중(DS 접속)인 유저면 kick 대기열 표시(service 위임) → DS가 폴링으로 회수(WS는 이미 끊긴 상태라 여기선 안 잡힘).
    const offSuperseded = onSuperseded((userId) =>
    {
        kickThisUserSockets(wss, userId);
        service.kickUserFromLiveMatch(userId);
    });

    // 죽은 연결 감지 — heartbeat 주기마다 pong 못 받은 소켓은 terminate.
    // 타이머 콜백에서 새는 예외는 uncaughtException이 되므로 주기 단위로 가둔다.
    const heartbeat = setInterval(() =>
    {
        try
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
        }
        catch (err)
        {
            logger.warn({ err }, 'WS heartbeat 실패 — 이번 주기 건너뜀');
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
            offSuperseded();
            clearInterval(heartbeat);
            clearInterval(cycle);
            // 확정 전 DS만 회수한다 — 진행 중인 경기는 백엔드 종료와 무관하게 끝나야 한다.
            allocator.shutdownUncommitted();
            for (const client of wss.clients)
            {
                client.terminate();
            }
            wss.close();
        },
    };
}

/** 업그레이드 핸드셰이크 본체 — 경로 확인 → JWT 인증 → connection으로 승격. */
async function handleUpgrade(wss: WebSocketServer, req: IncomingMessage, socket: Duplex, head: Buffer): Promise<void>
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
    const user = await authenticate(req);
    if (!user)
    {
        socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
        socket.destroy();

        return;
    }
    socket.removeListener('error', onSocketError);

    wss.handleUpgrade(req, socket, head, (ws) =>
    {
        // 해당 emit은 클라이언트를 향하지 않음. attachMatchWebSocket의 wss.on('connection')을 향한다.
        wss.emit('connection', ws, req, user);
    });
}

/**
 * 업그레이드 핸드셰이크의 Authorization 헤더에서 토큰 검증. 실패 시 null.
 * 단일 세션 강제: tokenVersion을 DB 현재값과 대조해 옛(대체된) 토큰의 매칭 접속도 거절.
 * verify·DB 조회 중 어떤 예외든 null(=401)로 수렴시켜 업그레이드 콜백의 unhandled reject를 막는다.
 */
async function authenticate(req: IncomingMessage): Promise<AuthedUser | null>
{
    const token = extractBearerToken(req.headers.authorization);
    if (!token)
    {
        return null;
    }

    try
    {
        const claims = jwtUtil.verify(token);

        const currentVersion = await getCurrentTokenVersion(claims.userId);
        if (currentVersion === null || currentVersion !== claims.tokenVersion)
        {
            return null;
        }

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

/** 해당 userId의 기존 소켓 정리(중복 탭/재연결) — 한 유저 한 자리 보장. except는 제외(신규 연결 자신). */
function kickThisUserSockets(wss: WebSocketServer, userId: number, except?: WebSocket): void
{
    for (const client of wss.clients)
    {
        const socket = client as AuthedWs;
        if (socket !== except && socket.userId === userId)
        {
            service.leave(userId);
            // 조용한 terminate 대신 사유를 실어 보낸 뒤 graceful close(4001) → 클라가 "다른 기기 로그인"으로 구분.
            send(socket, { type: 'session:invalid', ok: false });
            socket.close(4001, 'superseded');
        }
    }
}

/** 연결별 고정 윈도우 rate limit — 초과 메시지는 무시(윈도우당 경고 1회). */
function allowMessage(ws: AuthedWs): boolean
{
    const now = Date.now();
    if (now - ws.msgWindowStart >= config.rateLimit.windowMs)
    {
        ws.msgWindowStart = now;
        ws.msgCount = 0;
        ws.limitNotified = false;
    }
    ws.msgCount += 1;
    if (ws.msgCount <= config.rateLimit.wsMax)
    {
        return true;
    }
    if (!ws.limitNotified)
    {
        ws.limitNotified = true;
        sendError(ws, 'error', Codes.RATE_LIMITED, '메시지가 너무 잦습니다. 잠시 후 다시 시도해주세요.');
        logger.warn({ userId: ws.userId, count: ws.msgCount }, 'WS 메시지 rate limit 초과');
    }

    return false;
}

/** WS 메시지 처리 — queue:join / queue:cancel. */
async function onMessage(ws: AuthedWs, raw: RawData): Promise<void>
{
    if (!allowMessage(ws))
    {
        return;
    }

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
    newSocket.msgWindowStart = Date.now();
    newSocket.msgCount = 0;
    newSocket.limitNotified = false;

    // 이전 ws 소켓 Cleanup.
    kickThisUserSockets(wss, user.userId, newSocket);

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
    // 이 함수의 예외는 타이머 콜백 밖으로 나가 uncaughtException → 프로세스 종료가 된다.
    // 매치 하나의 실패가 진행 중인 다른 매치까지 끌고 죽지 않도록 사이클·매치 단위로 가둔다.
    try
    {
        // runMatching이 매칭 즉시 큐에서 제거하므로, 비동기 할당 중 재매칭 위험은 없다.
        const { groups, botFills } = service.runMatching(Date.now());
        for (const group of groups)
        {
            handleMatch(group).catch((err) =>
            {
                logger.error({ err, users: group.entries.map((e) => e.userId) }, '매치 처리 실패 — 이 매치만 폐기');
            });
        }
        // 장기 대기자는 봇 3명과 즉시 게임 투입(봇전).
        for (const entry of botFills)
        {
            handleBotMatch(entry).catch((err) =>
            {
                logger.error({ err, userId: entry.userId }, '봇전 처리 실패 — 이 매치만 폐기');
            });
        }
    }
    catch (err)
    {
        logger.error({ err }, '매칭 사이클 실패 — 다음 주기에 재시도');
    }
}
