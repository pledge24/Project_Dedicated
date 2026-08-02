// roster 도메인의 DB 쿼리 (repository 레이어).
// 백엔드 재시작을 건너 살아남은 DS의 serverToken을 검증하기 위한 영속 사본.
import type { RowDataPacket } from 'mysql2';

import { getPool } from '../common/db.js';
import type { MatchRoster, RosterPlayer } from './roster.types.js';

interface RosterRow extends RowDataPacket
{
    match_id: string;
    server_token: string;
    server_host: string | null;
    server_port: number | null;
    map_name: string;
    started_at: Date;
    players_json: RosterPlayer[] | string;
}

/** 매치 성사 시 명단 저장. 같은 matchId 재등록은 덮어쓴다(재시작 후 복원과 충돌 방지). */
export async function insert(roster: MatchRoster): Promise<void>
{
    await getPool().execute(
        'INSERT INTO match_rosters (match_id, server_token, server_host, server_port, map_name, started_at, players_json) ' +
        'VALUES (?, ?, ?, ?, ?, ?, ?) ' +
        'ON DUPLICATE KEY UPDATE server_token = VALUES(server_token), server_host = VALUES(server_host), ' +
        'server_port = VALUES(server_port), map_name = VALUES(map_name), ' +
        'started_at = VALUES(started_at), players_json = VALUES(players_json)',
        [roster.matchId, roster.serverToken, roster.server?.host ?? null, roster.server?.port ?? null,
            roster.mapName, new Date(roster.startedAt), JSON.stringify(roster.players)]
    );
}

/** 미확정 매치를 버릴 때 즉시 제거. */
export async function deleteById(matchId: string): Promise<void>
{
    await getPool().execute('DELETE FROM match_rosters WHERE match_id = ?', [matchId]);
}

/** 기동 시 복원 대상 — 아직 만료되지 않은 명단 전량. */
export async function listActive(sinceEpochMs: number): Promise<MatchRoster[]>
{
    const [rows] = await getPool().execute<RosterRow[]>(
        'SELECT match_id, server_token, server_host, server_port, map_name, started_at, players_json ' +
        'FROM match_rosters WHERE started_at > ? ORDER BY started_at ASC',
        [new Date(sinceEpochMs)]
    );

    return rows.map(toRoster);
}

/** 만료된 매치의 식별 정보 — abort 기록에 필요한 최소치(명단은 쓰지 않는다). */
export interface ExpiredRoster
{
    matchId: string;
    mapName: string;
    startedAt: number;
}

/** 만료분 조회 — 지우기 전에 "결과가 끝내 안 온 매치"를 가려내기 위해 먼저 읽는다. */
export async function listExpired(beforeEpochMs: number): Promise<ExpiredRoster[]>
{
    const [rows] = await getPool().execute<RosterRow[]>(
        'SELECT match_id, map_name, started_at FROM match_rosters WHERE started_at <= ?',
        [new Date(beforeEpochMs)]
    );

    return rows.map((row) => ({
        matchId: row.match_id,
        mapName: row.map_name,
        startedAt: row.started_at.getTime(),
    }));
}

/** 만료분 정리 — 메모리 sweep과 같은 기준(DS 최대 수명)으로 호출된다. */
export async function deleteExpired(beforeEpochMs: number): Promise<void>
{
    await getPool().execute('DELETE FROM match_rosters WHERE started_at <= ?', [new Date(beforeEpochMs)]);
}

/** 드라이버가 JSON 컬럼을 파싱해 주기도, 문자열로 주기도 해서(버전 의존) 양쪽을 흡수한다. */
function toRoster(row: RosterRow): MatchRoster
{
    const players = typeof row.players_json === 'string'
        ? (JSON.parse(row.players_json) as RosterPlayer[])
        : row.players_json;

    return {
        matchId: row.match_id,
        serverToken: row.server_token,
        mapName: row.map_name,
        startedAt: row.started_at.getTime(),
        players,
        server: row.server_host !== null && row.server_port !== null
            ? { host: row.server_host, port: row.server_port }
            : undefined,
    };
}
