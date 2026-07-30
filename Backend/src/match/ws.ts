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
import { getCurrentTokenVersion, onSuperseded } from '../common/session.js';
import type { AuthedUser } from '../common/types.js';
import { makeBotOpponents } from './bots.js';
import { allocator } from './dsAllocator.js';
import * as dsApiState from './dsApi.state.js';
import * as service from './matchmaking.service.js';
import type { ClientMessage, ServerMessage, ServerMessageType } from './protocol.js';
import type { MatchGroup, QueueEntry } from './queue.js';
import * as roster from './roster.js';

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
    // 게임중(DS 접속)인 유저면 별도로 kick 대기열에 표시 → DS가 폴링으로 회수(WS는 이미 끊긴 상태라 여기선 안 잡힘).
    const offSuperseded = onSuperseded((userId) =>
    {
        kickThisUserSockets(wss, userId);
        const matchId = roster.findMatchByUser(userId);
        if (matchId)
        {
            dsApiState.markKick(matchId, userId);
        }
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

/**
 * 한 매치 처리: 확정 창(끊김/재접속/취소) 방어 → DS 할당 → match:found.
 * 성사 즉시 큐에서 빠진 그룹을 begin/end/abortFormation으로 추적한다.
 * 끊김·재접속·취소는 전부 service.leave를 거쳐 inFormation에서 빠지므로 userId 기준으로 포착된다.
 */
async function handleMatch(group: MatchGroup<WebSocket>): Promise<void>
{
    const matchId = randomUUID();
    // DS 인증용 토큰. 커맨드라인으로 넘겨주며, DS -> 백엔드로 매치 결과 전송 시 사용.
    const serverToken = randomBytes(24).toString('base64url');
    const joinPlayers = group.entries.map((e) => ({
        ref: e.ref,
        userId: e.userId,
        nickname: e.nickname,
        // 각 플레이어 DS 입장 토큰. 클라가 DS 입장시 사용.
        joinToken: randomBytes(16).toString('base64url'),
    }));
    const userIds = group.entries.map((e) => e.userId);

    service.beginFormation(userIds);

    // 1) 스폰 전: 이미 닫힌 소켓(같은 틱 onClose 미처리 레이스)이 있으면 스폰 없이 생존자만 재큐.
    if (group.entries.some((e) => e.ref.readyState !== WebSocket.OPEN))
    {
        const requeued = service.abortFormation(group.entries);
        logger.warn({ matchId, requeued }, '확정 전 소켓 종료 — 스폰 취소, 생존자 재큐');

        return;
    }

    // 2) 매치를 실행할 DS 확보(로컬 프로세스든 stub이든 allocator가 결정).
    let server: { host: string; port: number };
    try
    {
        server = await allocator.allocate(matchId, serverToken, group.entries.length,
            joinPlayers.map((p) => ({ joinToken: p.joinToken, userId: p.userId, nickname: p.nickname })));
    }
    catch (err)
    {
        // 포트 고갈은 고아 DS나 진행 중 매치가 빠지면 풀리는 일시적 상태다 → 준비 타임아웃 경로(아래 4)와
        // 같이 생존자를 재큐한다. endFormation은 재큐를 안 해 대기자를 조용히 떨어뜨렸다.
        // error를 보내지 않는 것도 의도적 — 클라의 error 핸들러는 MatchmakingState를 Idle로 되돌리지 않아
        // (queue:left와 달리) 재큐와 조합하면 서버/클라 상태가 어긋난다.
        const requeued = service.abortFormation(group.entries);
        logger.error({ err, matchId, requeued }, 'DS 할당 실패 — 생존자 재큐');

        return;
    }

    // 3) roster 등록을 spawn 직후로 앞당김 — DS의 ready 콜백을 assertServerToken으로 인증하려면 명단이 있어야 함.
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

    // 4) DS가 "플레이어 받을 준비됨"을 통지할 때까지 대기(stub은 즉시 통과). 실패 시 회수 + 생존자 재큐.
    try
    {
        await allocator.waitUntilReady(matchId);
    }
    catch (err)
    {
        allocator.release(server.port);
        roster.remove(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation(group.entries);
        logger.warn({ err, matchId, requeued }, 'DS 준비 실패/타임아웃 — DS 회수, 생존자 재큐');

        return;
    }

    // 5) 준비 대기 사이 끊김/재접속/취소 포착(userId 기준). 하나라도 이탈 시 DS 회수 + roster 제거 + 생존자 재큐.
    if (group.entries.some((e) => !service.isInFormation(e.userId) || e.ref.readyState !== WebSocket.OPEN))
    {
        allocator.release(server.port);
        roster.remove(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation(group.entries);
        logger.warn({ matchId, requeued }, '확정 창 이탈 — DS 회수, 생존자 재큐');

        return;
    }

    // 6) 성사 확정: formation 종료 → 각 클라에 본인 입장 토큰만 실어 push(roster는 3)에서 이미 등록).
    service.endFormation(userIds);
    // 여기서부터 이 DS는 독립 워크로드다 — 백엔드가 죽어도 경기는 끝까지 간다.
    allocator.commit(matchId);
    const { data } = service.buildMatchFound(group, matchId, server);
    for (const p of joinPlayers)
    {
        send(p.ref, { type: 'match:found', ok: true, data: { ...data, joinToken: p.joinToken } });
    }
    logger.info({ matchId, server, players: joinPlayers.map((p) => p.userId) }, '매치 성사');
}

/**
 * 봇전 처리: 장기 대기 유저 1명 + 봇(playersPerMatch-1)명. 봇은 소켓·토큰 없이 DS가 서버측 스폰한다.
 * 봇은 roster에 sentinel userId·rating으로 등록 → 결과 저장 시 ELO엔 포함되나 DB write는 실제 유저만.
 * handleMatch의 확정 창 방어(부팅 중 끊김/취소)를 소켓 1개 기준으로 축약해 미러한다.
 */
async function handleBotMatch(entry: QueueEntry<WebSocket>): Promise<void>
{
    const matchId = randomUUID();
    const serverToken = randomBytes(24).toString('base64url');
    const joinToken = randomBytes(16).toString('base64url');
    const bots = makeBotOpponents(entry.score, config.match.playersPerMatch - 1,
        config.match.botFill.ratingSpread, config.match.scoreFloor, config.match.scoreCeiling);

    service.beginFormation([entry.userId]);

    // 1) 스폰 전: 이미 닫힌 소켓이면 취소.
    if (entry.ref.readyState !== WebSocket.OPEN)
    {
        service.endFormation([entry.userId]);
        logger.warn({ matchId, userId: entry.userId }, '봇전 확정 전 소켓 종료 — 취소');

        return;
    }

    // 2) 봇전을 실행할 DS spawn(또는 stub). ExpectedPlayers=총원(휴먼+봇). 봇은 -Bots=로만 전달.
    let server: { host: string; port: number };
    try
    {
        server = await allocator.allocate(matchId, serverToken, config.match.playersPerMatch,
            [{ joinToken, userId: entry.userId, nickname: entry.nickname }],
            bots.map((b) => ({ userId: b.userId, nickname: b.nickname })));
    }
    catch (err)
    {
        // handleMatch와 동일 — 일시적 고갈이므로 재큐(봇전 대기 시간도 원 joinedAt으로 보존된다).
        const requeued = service.abortFormation([entry]);
        logger.error({ err, matchId, userId: entry.userId, requeued }, '봇전 DS 할당 실패 — 재큐');

        return;
    }

    // 3) roster 등록(휴먼 + 봇)을 spawn 직후로 앞당김 — DS의 ready 콜백 인증(assertServerToken)에 명단 필요.
    roster.register({
        matchId,
        serverToken,
        mapName: config.match.ds.map,
        startedAt: Date.now(),
        players: [
            { userId: entry.userId, nickname: entry.nickname, joinToken },
            ...bots.map((b) => ({ userId: b.userId, nickname: b.nickname, joinToken: '', bot: true, rating: b.rating })),
        ],
    });

    // 4) DS 준비 통지 대기(stub은 즉시 통과). 실패 시 DS 회수 + roster 제거 + 휴먼 재큐.
    try
    {
        await allocator.waitUntilReady(matchId);
    }
    catch (err)
    {
        allocator.release(server.port);
        roster.remove(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation([entry]);
        logger.warn({ err, matchId, userId: entry.userId, requeued }, '봇전 DS 준비 실패/타임아웃 — DS 회수');

        return;
    }

    // 5) 준비 대기 사이 끊김/재접속/취소 포착. 이탈 시 DS 회수 + roster 제거 + 생존 시 재큐.
    if (!service.isInFormation(entry.userId) || entry.ref.readyState !== WebSocket.OPEN)
    {
        allocator.release(server.port);
        roster.remove(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation([entry]);
        logger.warn({ matchId, userId: entry.userId, requeued }, '봇전 확정 창 이탈 — DS 회수');

        return;
    }

    // 6) 성사 확정: 휴먼에 match:found(roster는 3)에서 이미 등록).
    service.endFormation([entry.userId]);
    allocator.commit(matchId);
    send(entry.ref, { type: 'match:found', ok: true, data: { matchId, server: { host: server.host, port: server.port }, joinToken } });
    logger.info({ matchId, server, userId: entry.userId, bots: bots.length }, '봇전 성사');
}
