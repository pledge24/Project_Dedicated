// D1 백엔드 상태 조회 MCP 서버 (읽기 전용, stdio).
//
// 왜 DB만 읽는가: 백엔드에 진단용 HTTP 엔드포인트를 여는 쪽이 정보는 더 많지만,
// 이 프로젝트의 핵심 주장이 "서버 권위"인데 그 서버에 새 공격면을 여는 건 값이 안 맞는다.
// roster가 DB에 영속되므로(match_rosters) 진행 중 매치까지는 DB만으로 답할 수 있다.
// 답할 수 없는 것: 아직 매칭되지 않은 대기열(살아있는 WebSocket이라 프로세스 밖에서 안 보인다).
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { CallToolRequestSchema, ListToolsRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import 'dotenv/config';
import mysql from 'mysql2/promise';

const pool = mysql.createPool({
    host: process.env.DB_HOST || '127.0.0.1',
    port: Number(process.env.DB_PORT || 3306),
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASS || '',
    database: process.env.DB_NAME || 'd1',
    connectionLimit: 4,
    charset: 'utf8mb4',
});

const TOOLS = [
    {
        name: 'inspect_player',
        description: '플레이어 한 명의 현재 상태 — 프로필(점수/레벨/전적), 진행 중인 매치, 최근 매치 결과. nickname 또는 userId 중 하나를 준다.',
        inputSchema: {
            type: 'object',
            properties: {
                nickname: { type: 'string', description: '닉네임(정확히 일치)' },
                userId: { type: 'number', description: '유저 ID' },
                recentLimit: { type: 'number', description: '최근 매치 개수 (기본 5, 최대 20)' },
            },
        },
    },
    {
        name: 'list_active_matches',
        description: '진행 중인 매치 목록 — match_rosters 기준. 각 매치의 DS 주소, 참가자, 경과 시간, 결과 보고 여부.',
        inputSchema: {
            type: 'object',
            properties: {
                includeFinished: { type: 'boolean', description: '결과가 이미 저장된 매치도 포함 (기본 false)' },
            },
        },
    },
];

const server = new Server(
    { name: 'd1-backend-mcp', version: '0.1.0' },
    { capabilities: { tools: {} } }
);

server.setRequestHandler(ListToolsRequestSchema, () => ({ tools: TOOLS }));

server.setRequestHandler(CallToolRequestSchema, async (req) =>
{
    const { name, arguments: args = {} } = req.params;
    try
    {
        const result = name === 'inspect_player' ? await inspectPlayer(args)
            : name === 'list_active_matches' ? await listActiveMatches(args)
                : { error: `알 수 없는 도구: ${name}` };

        return { content: [{ type: 'text', text: JSON.stringify(result, null, 2) }] };
    }
    catch (err)
    {
        // 도구 실패는 프로토콜 에러가 아니라 결과로 돌려준다 — 세션이 끊기지 않아야 한다.
        return {
            content: [{ type: 'text', text: JSON.stringify({ error: String(err?.message ?? err) }, null, 2) }],
            isError: true,
        };
    }
});

/** 프로필 + 진행 중 매치 + 최근 전적. */
async function inspectPlayer({ nickname, userId, recentLimit })
{
    if (!nickname && !userId)
    {
        return { error: 'nickname 또는 userId 중 하나가 필요합니다.' };
    }

    const [profiles] = await pool.query(
        'SELECT u.id AS userId, u.login_id AS loginId, u.nickname, p.score, p.level, p.exp, ' +
        'p.wins, p.losses, p.matches_played AS matchesPlayed, p.last_match_at AS lastMatchAt ' +
        'FROM users u JOIN player_profiles p ON p.user_id = u.id ' +
        (userId ? 'WHERE u.id = ?' : 'WHERE u.nickname = ?'),
        [userId ?? nickname]
    );
    if (profiles.length === 0)
    {
        return { found: false };
    }

    const me = profiles[0];
    const limit = Math.min(20, Math.max(1, Number(recentLimit) || 5));

    // 진행 중 매치 — roster에 있고 아직 결과가 없는 것.
    // JSON_SEARCH는 문자열만 찾으므로 숫자 userId에는 쓸 수 없다 → userId만 뽑아 배열 포함 검사.
    const [active] = await pool.query(
        'SELECT r.match_id AS matchId, r.server_host AS host, r.server_port AS port, r.started_at AS startedAt ' +
        'FROM match_rosters r ' +
        'LEFT JOIN matches m ON m.client_match_id = r.match_id ' +
        "WHERE m.id IS NULL AND JSON_CONTAINS(JSON_EXTRACT(r.players_json, '$[*].userId'), CAST(? AS JSON))",
        [String(me.userId)]
    );

    const [recent] = await pool.query(
        'SELECT m.client_match_id AS matchId, m.map_name AS mapName, m.end_reason AS endReason, ' +
        'm.ended_at AS endedAt, mp.placement, mp.score_delta AS scoreDelta, mp.exp_gained AS expGained, ' +
        'mp.abandoned FROM match_participants mp JOIN matches m ON m.id = mp.match_id ' +
        `WHERE mp.user_id = ? ORDER BY m.ended_at DESC LIMIT ${limit}`,
        [me.userId]
    );

    return { found: true, profile: me, activeMatches: active, recent };
}

/** 진행 중(또는 전체) 매치 목록. */
async function listActiveMatches({ includeFinished = false })
{
    const [rows] = await pool.query(
        'SELECT r.match_id AS matchId, r.server_host AS host, r.server_port AS port, ' +
        'r.map_name AS mapName, r.started_at AS startedAt, r.players_json AS players, ' +
        'm.end_reason AS endReason FROM match_rosters r ' +
        'LEFT JOIN matches m ON m.client_match_id = r.match_id ' +
        (includeFinished ? '' : 'WHERE m.id IS NULL ') +
        'ORDER BY r.started_at DESC'
    );

    const now = Date.now();

    return {
        count: rows.length,
        matches: rows.map((r) =>
        {
            // 드라이버 버전에 따라 JSON 컬럼이 파싱돼 오기도, 문자열로 오기도 한다.
            const players = typeof r.players === 'string' ? JSON.parse(r.players) : r.players;

            return {
                matchId: r.matchId,
                server: r.host ? `${r.host}:${r.port}` : null,
                mapName: r.mapName,
                startedAt: r.startedAt,
                elapsedSec: Math.round((now - new Date(r.startedAt).getTime()) / 1000),
                reported: r.endReason ?? null,
                // joinToken은 신원 도용 권한이므로 내보내지 않는다.
                players: (players ?? []).map((p) => ({ userId: p.userId, nickname: p.nickname, bot: !!p.bot })),
            };
        }),
    };
}

await server.connect(new StdioServerTransport());
