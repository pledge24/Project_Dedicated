// 매치 성사 처리 (handler 레이어) — WS 사이클이 그룹을 넘기면 여기서 한 판을 조립한다.
// ws.ts에서 분리한 이유: 토큰 발급·roster 등록·확정 창 방어는 도메인 결정이지 소켓 배관이 아니다.
// (network 파일이 596줄까지 커진 원인이기도 했다.)
//
// 여기가 하는 일 — 실패하면 어디까지 되돌리는지가 이 파일의 전부다:
//   토큰 발급 → DS 할당 → roster 등록 → 준비 대기 → 확정 창 재검사 → match:found 푸시
// 각 단계 실패는 그 앞 단계까지만 되돌리고 생존자를 재큐한다(매치 하나의 실패가 대기자를 떨어뜨리면 안 된다).
import { randomBytes, randomUUID } from 'node:crypto';
import { WebSocket } from 'ws';

import { config } from '../common/config.js';
import { logger } from '../common/logger.js';
import { makeBotOpponents } from './bots.js';
import { allocator } from './dsAllocator.js';
import * as dsApiState from './dsApi.state.js';
import * as service from './matchmaking.service.js';
import type { MatchGroup, QueueEntry } from './queue.js';
import * as roster from './roster.js';
import { send } from './wsSend.js';

/**
 * 한 매치 처리: 확정 창(끊김/재접속/취소) 방어 → DS 할당 → match:found.
 * 성사 즉시 큐에서 빠진 그룹을 begin/end/abortFormation으로 추적한다.
 * 끊김·재접속·취소는 전부 service.leave를 거쳐 inFormation에서 빠지므로 userId 기준으로 포착된다.
 */
export async function handleMatch(group: MatchGroup<WebSocket>): Promise<void>
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
    //    DB 저장 실패 시 매치를 버린다: roster 없이 진행하면 ready 콜백도 결과 보고도 404가 된다.
    try
    {
        await roster.register({
            matchId,
            serverToken,
            server,
            mapName: config.match.ds.map,
            startedAt: Date.now(),
            players: joinPlayers.map((p) => ({
                userId: p.userId,
                nickname: p.nickname,
                joinToken: p.joinToken,
            })),
        });
    }
    catch (err)
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation(group.entries);
        logger.error({ err, matchId, requeued }, 'roster 등록 실패 — DS 회수, 생존자 재큐');

        return;
    }

    // 4) DS가 "플레이어 받을 준비됨"을 통지할 때까지 대기(stub은 즉시 통과). 실패 시 회수 + 생존자 재큐.
    try
    {
        await allocator.waitUntilReady(matchId);
    }
    catch (err)
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation(group.entries);
        await removeRosterQuietly(matchId);
        logger.warn({ err, matchId, requeued }, 'DS 준비 실패/타임아웃 — DS 회수, 생존자 재큐');

        return;
    }

    // 5) 준비 대기 사이 끊김/재접속/취소 포착(userId 기준). 하나라도 이탈 시 DS 회수 + roster 제거 + 생존자 재큐.
    if (group.entries.some((e) => !service.isInFormation(e.userId) || e.ref.readyState !== WebSocket.OPEN))
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation(group.entries);
        await removeRosterQuietly(matchId);
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
export async function handleBotMatch(entry: QueueEntry<WebSocket>): Promise<void>
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
    try
    {
        await roster.register({
            matchId,
            serverToken,
            server,
            mapName: config.match.ds.map,
            startedAt: Date.now(),
            players: [
                { userId: entry.userId, nickname: entry.nickname, joinToken },
                ...bots.map((b) => ({ userId: b.userId, nickname: b.nickname, joinToken: '', bot: true, rating: b.rating })),
            ],
        });
    }
    catch (err)
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation([entry]);
        logger.error({ err, matchId, userId: entry.userId, requeued }, '봇전 roster 등록 실패 — DS 회수, 재큐');

        return;
    }

    // 4) DS 준비 통지 대기(stub은 즉시 통과). 실패 시 DS 회수 + roster 제거 + 휴먼 재큐.
    try
    {
        await allocator.waitUntilReady(matchId);
    }
    catch (err)
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation([entry]);
        await removeRosterQuietly(matchId);
        logger.warn({ err, matchId, userId: entry.userId, requeued }, '봇전 DS 준비 실패/타임아웃 — DS 회수');

        return;
    }

    // 5) 준비 대기 사이 끊김/재접속/취소 포착. 이탈 시 DS 회수 + roster 제거 + 생존 시 재큐.
    if (!service.isInFormation(entry.userId) || entry.ref.readyState !== WebSocket.OPEN)
    {
        allocator.release(matchId);
        dsApiState.clear(matchId);
        const requeued = service.abortFormation([entry]);
        await removeRosterQuietly(matchId);
        logger.warn({ matchId, userId: entry.userId, requeued }, '봇전 확정 창 이탈 — DS 회수');

        return;
    }

    // 6) 성사 확정: 휴먼에 match:found(roster는 3)에서 이미 등록).
    service.endFormation([entry.userId]);
    allocator.commit(matchId);
    send(entry.ref, { type: 'match:found', ok: true, data: { matchId, server: { host: server.host, port: server.port }, joinToken } });
    logger.info({ matchId, server, userId: entry.userId, bots: bots.length }, '봇전 성사');
}

/**
 * 롤백 경로의 roster 정리 — DB 실패가 재큐를 막아선 안 된다.
 * 남더라도 미확정 매치라 DS가 이미 회수됐고, 만료 sweep이 결국 치운다.
 * (호출 순서상 메모리 롤백을 먼저 끝낸 뒤 호출한다.)
 */
async function removeRosterQuietly(matchId: string): Promise<void>
{
    try
    {
        await roster.remove(matchId);
    }
    catch (err)
    {
        logger.warn({ err, matchId }, 'roster 제거 실패 — 만료 sweep에 위임');
    }
}
