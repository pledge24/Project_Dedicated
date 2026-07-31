// 매치 결과 저장 (repository 레이어) — matches + match_participants + player_profiles를 한 트랜잭션으로.
// ELO는 현재 점수에 의존하므로 트랜잭션 안에서 FOR UPDATE로 점수를 잠그고 재조회한 뒤 계산한다.
import type { ResultSetHeader, RowDataPacket } from 'mysql2';
import type { PoolConnection } from 'mysql2/promise';

import { config } from '../common/config.js';
import { getPool, withTransaction } from '../common/db.js';
import type { MatchEndReason } from '../common/types.js';
import { computeFfaEloDeltas } from './elo.js';

/** saveResult 함수용 - 종료 매치 각 플레이어 정보 */
export interface SaveResultParticipant
{
    userId: number;
    slotIndex: number;
    nicknameSnapshot: string;
    placement: number;
    livesLeft: number;
    left: boolean;                // 게임중 다른 기기 로그인으로 kick된 탈주자(전원 꼴등, 최하위 확정값)
    bot?: boolean;                // 봇전 봇(DB 미존재). ELO 입력엔 포함, 프로필/participants 기록은 skip.
    rating?: number;              // 봇 ELO 입력 점수(백엔드 소유). 봇에만 존재.
}

/** saveResult 함수용 - 입력 매개변수 구조 */
export interface SaveResultInput
{
    matchId: string;          // client_match_id (UNIQUE → 멱등성 가드)
    mapName: string;
    startedAt: Date;
    endedAt: Date;
    durationSec: number;
    endReason: MatchEndReason;
    winnerUserId: number | null;
    participants: SaveResultParticipant[];
}

/** ELO 계산 결과 반환 구조체 */
export interface ParticipantScoreResult
{
    userId: number;
    placement: number;
    scoreBefore: number;
    scoreDelta: number;
    scoreAfter: number;
}

interface ProfileRow extends RowDataPacket
{
    user_id: number;
    score: number;
    exp: number;
}

/** 탈주 정산 원장(match_leaver_settlements) 조회 행. */
interface SettlementRow extends RowDataPacket
{
    score_delta: number;
    score_after: number;
}

/** 즉시 정산 결과 — 프로필 하락값. /leaver 응답 + /result 참가자 row에 실린다. */
export interface SettledLeaver
{
    scoreDelta: number;
    scoreAfter: number;
}

// 등수별 경험치(1~4등). placement 범위를 벗어나면 최저값.
const PLACEMENT_EXP = [100, 70, 40, 20];

// 등수별 기본 배점(1~4등). 1·2위 양수, 4위 음수 → 매치 총합이 순양수(인플레이션).
// 3위는 0이라 ELO항이 부호를 결정(소폭 +/−). 순수 ELO(elo.ts)에 이 값을 더해 clamp한다.
const PLACEMENT_BASE_POINTS = [45, 20, 0, -35];

// 레벨당 필요 경험치(전 구간 균일). level = floor(exp / EXP_PER_LEVEL) + 1.
const EXP_PER_LEVEL = 1000;

/** 결과를 원자적으로 기록하고 ELO로 점수를 갱신한다. matchId 중복 시 ER_DUP_ENTRY를 throw. */
export async function saveResult(input: SaveResultInput): Promise<ParticipantScoreResult[]>
{
    return withTransaction(async (conn) =>
    {
        // matches 테이블에 결과 저장.
        const [matchRes] = await conn.execute<ResultSetHeader>(
            'INSERT INTO matches ' +
            '(client_match_id, map_name, started_at, ended_at, duration_sec, end_reason, winner_user_id) ' +
            'VALUES (?, ?, ?, ?, ?, ?, ?)',
            [
                input.matchId, input.mapName, input.startedAt, input.endedAt,
                input.durationSec, input.endReason, input.winnerUserId,
            ]
        );
        const matchDbId = matchRes.insertId;

        // 탈주자(left)와 완주자를 분리. ELO는 완주자끼리만, 탈주자는 최하위 확정값(원장 정산).
        // 완주자 중 봇전 봇은 DB에 없어 프로필 잠금·기록에서 제외하되, ELO 입력엔 포함(플레이어 delta가 4인전과 동일).
        const finishers = input.participants.filter((p) => !p.left);
        const realFinishers = finishers.filter((p) => !p.bot);
        const leavers = input.participants.filter((p) => p.left); // 봇은 탈주 안 함(전부 실제 유저)

        // 완주자(실유저)+탈주자 프로필을 한 번에 FOR UPDATE로 잠근다(정렬 순서라 데드락 없음).
        // 잠근 뒤에야 원장을 읽어야 경쟁 /leaver 트랜잭션의 커밋된 정산을 관측한다
        // (불변식: 프로필 잠금 → 원장 조회 순서. 원장 조회 전에 다른 평문 SELECT 금지).
        const lockUserIds = [...realFinishers, ...leavers].map((p) => p.userId);
        const profileByUser = lockUserIds.length > 0
            ? await lockAndFetchProfiles(conn, lockUserIds)
            : new Map<number, { score: number; exp: number }>();

        const rows: ComputedRow[] = [];

        // ── 완주자: 실제 유저는 잠근 점수로, 봇은 주입 rating. FFA ELO는 완주자 전원으로 계산 ──
        if (finishers.length > 0)
        {
            const ratings = finishers.map((p) => (p.bot ? p.rating! : profileByUser.get(p.userId)!.score));
            const placements = finishers.map((p) => p.placement);
            const deltas = computeFfaEloDeltas(ratings, placements, config.match.eloK);
            finishers.forEach((p, i) =>
            {
                if (p.bot)
                {
                    return; // 봇: 점수 누적처 없음 → row 생성·프로필 갱신 안 함(delta는 계산에만 기여)
                }
                const c = computeParticipantResult(p.placement, ratings[i], deltas[i],
                    config.match.scoreFloor, config.match.scoreCeiling, profileByUser.get(p.userId)!.exp);
                rows.push({ p, c, updateProfile: true });
            });
        }

        // ── 탈주자: 원장에 이미 정산됐으면(/leaver 즉시정산) 그 값 재사용, 없으면 지금 정산(escape 방지) ──
        for (const p of leavers)
        {
            const existing = await selectSettlement(conn, input.matchId, p.userId);
            const info = existing
                ?? await applyLeaverPenalty(conn, input.matchId, p.userId, profileByUser.get(p.userId)!.score, input.endedAt);
            rows.push({
                p,
                // 프로필은 /leaver 또는 위 applyLeaverPenalty가 이미 기록 → 여기선 재갱신 skip.
                c: { scoreBefore: info.scoreAfter - info.scoreDelta, scoreDelta: info.scoreDelta, scoreAfter: info.scoreAfter, expGained: 0, isWin: 0, levelAfter: 0 },
                updateProfile: false,
            });
        }

        // match_participants는 multi-row INSERT 1회. VALUES 그룹만 동적 생성(주입 위험 0).
        // DB 컬럼명은 abandoned 유지(마이그레이션 생략) — 값은 left 플래그.
        const rowsSql = rows.map(() => '(?, ?, ?, ?, ?, ?, ?, ?, ?)').join(', ');
        const insertParams = rows.flatMap(({ p, c }) =>
            [matchDbId, p.userId, p.nicknameSnapshot, p.slotIndex, p.placement, p.livesLeft, c.expGained, c.scoreDelta, p.left ? 1 : 0]);
        await conn.execute(
            'INSERT INTO match_participants ' +
            '(match_id, user_id, nickname_snapshot, slot_index, placement, lives_left, exp_gained, score_delta, abandoned) ' +
            `VALUES ${rowsSql}`,
            insertParams
        );

        // player_profiles는 행마다 값이 달라 개별 UPDATE. 정산 완료 탈주자(updateProfile=false)는 skip.
        const saved: ParticipantScoreResult[] = [];
        for (const { p, c, updateProfile } of rows)
        {
            if (updateProfile)
            {
                await conn.execute(
                    'UPDATE player_profiles SET ' +
                    'score = ?, wins = wins + ?, losses = losses + ?, matches_played = matches_played + 1, ' +
                    'exp = exp + ?, level = ?, last_match_at = ?, ' +
                    // 점수가 실제로 바뀐 매치만 갱신 시점 기록 (±0은 유지) → 랭킹 동점 순위 보호.
                    'score_updated_at = CASE WHEN ? <> 0 THEN ? ELSE score_updated_at END ' +
                    'WHERE user_id = ?',
                    [c.scoreAfter, c.isWin, 1 - c.isWin, c.expGained, c.levelAfter, input.endedAt, c.scoreDelta, input.endedAt, p.userId]
                );
            }

            saved.push({
                userId: p.userId,
                placement: p.placement,
                scoreBefore: c.scoreBefore,
                scoreDelta: c.scoreDelta,
                scoreAfter: c.scoreAfter,
            });
        }

        return saved;
    });
}

/** 참가자별 파생값 + 프로필 갱신 여부. */
interface ComputedRow
{
    p: SaveResultParticipant;
    c: { scoreBefore: number; scoreDelta: number; scoreAfter: number; expGained: number; isWin: number; levelAfter: number };
    updateProfile: boolean;
}

/**
 * 탈주자 즉시 정산(/leaver) — kick/이탈 시점(매치 종료 전)에 프로필 점수를 바로 하락시켜 로비에 반영.
 * 프로필을 FOR UPDATE로 잠근 뒤 원장을 확인 → 이미 정산됐으면 그대로 반환(멱등),
 * 아니면 applyLeaverPenalty. /result(saveResult)와 같은 원장·잠금을 공유해 정확히 1회만 적용된다.
 */
export async function settleLeaverProfile(clientMatchId: string, userId: number): Promise<SettledLeaver>
{
    return withTransaction(async (conn) =>
    {
        const profileByUser = await lockAndFetchProfiles(conn, [userId]);
        const existing = await selectSettlement(conn, clientMatchId, userId);
        if (existing)
        {
            return existing;
        }

        return applyLeaverPenalty(conn, clientMatchId, userId, profileByUser.get(userId)!.score, new Date());
    });
}

/**
 * 이 매치의 결과가 이미 저장됐는가. 재입장 판정용 — 결과가 있으면 경기는 끝난 것이다.
 * roster는 재제출 멱등을 위해 종료 후에도 sweep 전까지 남으므로, roster 존재만으로는 진행 중을 알 수 없다.
 */
export async function hasResult(clientMatchId: string): Promise<boolean>
{
    const [rows] = await getPool().execute<RowDataPacket[]>(
        'SELECT 1 FROM matches WHERE client_match_id = ? LIMIT 1',
        [clientMatchId]
    );

    return rows.length > 0;
}

/**
 * 이 유저가 이 매치에서 이미 탈주로 정산됐는가. 재입장 판정용.
 * DS는 매치 시작 후 이탈자를 KickedUserIds에 넣어 재입장을 거절하므로(D1MatchFlowComponent),
 * 정산된 유저에게 주소를 주면 DS가 튕겨낸다 — 백엔드에서 미리 거른다.
 */
export async function isLeaverSettled(clientMatchId: string, userId: number): Promise<boolean>
{
    const [rows] = await getPool().execute<RowDataPacket[]>(
        'SELECT 1 FROM match_leaver_settlements WHERE client_match_id = ? AND user_id = ? LIMIT 1',
        [clientMatchId, userId]
    );

    return rows.length > 0;
}

/**
 * 탈주 패널티(최하위 확정값 = base[꼴등] + 탈주 감점, ELO 무관)를 적용하고 멱등 원장에 기록.
 * 호출부가 해당 프로필 행을 FOR UPDATE로 잠근 상태여야 한다. /leaver·/result 두 경로가 공유.
 * 프로필 UPDATE는 점수·패배·판수·시각만 — level/exp/wins는 손대지 않는다(완주자 UPDATE와 다름).
 */
async function applyLeaverPenalty(conn: PoolConnection, clientMatchId: string, userId: number, currentScore: number, now: Date): Promise<SettledLeaver>
{
    const after = computeLeaverAfter(currentScore, config.match.scoreFloor, config.match.scoreCeiling, config.match.leaverPenalty);
    const scoreDelta = after - currentScore;

    await conn.execute(
        'UPDATE player_profiles SET ' +
        'score = ?, losses = losses + 1, matches_played = matches_played + 1, last_match_at = ?, ' +
        'score_updated_at = CASE WHEN ? <> 0 THEN ? ELSE score_updated_at END ' +
        'WHERE user_id = ?',
        [after, now, scoreDelta, now, userId]
    );

    await conn.execute(
        'INSERT INTO match_leaver_settlements (client_match_id, user_id, score_delta, score_after, settled_at) ' +
        'VALUES (?, ?, ?, ?, ?)',
        [clientMatchId, userId, scoreDelta, after, now]
    );

    return { scoreDelta, scoreAfter: after };
}

/**
 * 탈주 정산 원장 단건 조회. **반드시 비잠금 평문 SELECT** — FOR UPDATE로 바꾸면 서로 다른
 * 탈주자의 /leaver가 같은 매치 빈 범위에 gap-lock을 잡고 INSERT 시 insert-intention 충돌 →
 * 다중 동시 탈주에서 데드락. 호출부의 프로필 FOR UPDATE가 선행하므로, 이 평문 SELECT의 read-view는
 * 잠금 획득 후 형성되어 경쟁 트랜잭션의 커밋된 정산을 관측한다(원장 조회 앞에 다른 SELECT 금지).
 */
async function selectSettlement(conn: PoolConnection, clientMatchId: string, userId: number): Promise<SettledLeaver | null>
{
    const [rows] = await conn.query<SettlementRow[]>(
        'SELECT score_delta, score_after FROM match_leaver_settlements WHERE client_match_id = ? AND user_id = ?',
        [clientMatchId, userId]
    );

    return rows.length > 0 ? { scoreDelta: rows[0].score_delta, scoreAfter: rows[0].score_after } : null;
}

/** 참가자 점수·경험치를 FOR UPDATE로 잠그고 재조회. 잠근 행 수가 요청과 다르면 즉시 throw(침묵 오염 방지). */
async function lockAndFetchProfiles(conn: PoolConnection, userIds: number[]): Promise<Map<number, { score: number; exp: number }>>
{
    const placeholders = userIds.map(() => '?').join(', ');
    const [rows] = await conn.query<ProfileRow[]>(
        `SELECT user_id, score, exp FROM player_profiles WHERE user_id IN (${placeholders}) FOR UPDATE`,
        userIds
    );
    if (rows.length !== userIds.length)
    {
        throw new Error(`프로필 잠금 행 수 불일치: 요청 ${userIds.length}, 조회 ${rows.length}`);
    }

    const profileByUser = new Map<number, { score: number; exp: number }>();
    for (const row of rows)
    {
        profileByUser.set(Number(row.user_id), { score: row.score, exp: row.exp });
    }

    return profileByUser;
}

/** 완주자 한 명의 점수·레벨 파생값(순수). floor·ceiling 적용 후 실제 변화량을 scoreDelta로 반환 → before+delta=after 보장. */
function computeParticipantResult(placement: number, before: number, delta: number, scoreFloor: number, scoreCeiling: number, expBefore: number)
{
    // delta는 순수 ELO항. 등수 기본배점을 더한 뒤 상·하한으로 clamp.
    const after = Math.min(scoreCeiling, Math.max(scoreFloor, before + delta + basePointsForPlacement(placement)));
    const expGained = expForPlacement(placement);

    return {
        scoreBefore: before,
        scoreDelta: after - before,       // 반영된 실변화량(clamp 반영)
        scoreAfter: after,
        isWin: placement === 1 ? 1 : 0,
        expGained,
        levelAfter: levelForExp(expBefore + expGained),
    };
}

/** 탈주자 최하위 확정값(순수) — base[꼴등] + 탈주 감점, clamp. 즉시 정산 전용. */
function computeLeaverAfter(before: number, scoreFloor: number, scoreCeiling: number, leaverPenalty: number): number
{
    const lastPlacement = config.match.playersPerMatch;

    return Math.min(scoreCeiling, Math.max(scoreFloor, before + basePointsForPlacement(lastPlacement) - leaverPenalty));
}

/** 등수별 획득 경험치. 범위를 벗어나면 최저값. */
function expForPlacement(placement: number): number
{
    return PLACEMENT_EXP[placement - 1] ?? PLACEMENT_EXP[PLACEMENT_EXP.length - 1];
}

/** 등수별 기본 배점. 범위를 벗어나면 최저값(꼴찌 취급). */
function basePointsForPlacement(placement: number): number
{
    return PLACEMENT_BASE_POINTS[placement - 1] ?? PLACEMENT_BASE_POINTS[PLACEMENT_BASE_POINTS.length - 1];
}

/** 누적 경험치로 레벨 산출. 전 구간 EXP_PER_LEVEL당 1레벨 (Lv.1 = exp 0~999). */
function levelForExp(exp: number): number
{
    return Math.floor(exp / EXP_PER_LEVEL) + 1;
}
