// 도메인 타입 정의 (interface 모음).
import type { RowDataPacket } from 'mysql2';

export interface BackendError
{
    code: string;
    message: string;
}

export interface BackendResponse<T = unknown>
{
    ok: boolean;
    data?: T;
    error?: BackendError;
}

/** 인증 미들웨어가 req.user에 주입하는 토큰 클레임. */
export interface AuthedUser
{
    userId: number;
    nickname: string;
}

export interface AuthUserDTO
{
    userId: number;
    nickname: string;
    score: number;
    level: number;
    exp: number;
    token: string;
}

export interface RegisterResultDTO
{
    userId: number;
    nickname: string;
    score: number;
    level: number;
    exp: number;
}

/** DB users 행. mysql2 execute<UserRow[]> 제네릭에 쓰려고 RowDataPacket 확장. */
export interface UserRow extends RowDataPacket
{
    id: number;
    login_id: string;
    password_hash: string;
    nickname: string;
}

/** DB player_profiles 행. */
export interface PlayerProfileRow extends RowDataPacket
{
    user_id: number;
    score: number;
    level: number;
    exp: number;
    wins: number;
    losses: number;
    matches_played: number;
    last_match_at: Date | null;
}

export type MatchEndReason = 'winner' | 'draw' | 'time_expired' | 'abort';

export interface MatchRowDTO
{
    id: number;
    clientMatchId: string;
    mapName: string;
    startedAt: string;
    endedAt: string;
    durationSec: number;
    endReason: MatchEndReason;
    winnerUserId: number | null;
}

export interface MatchParticipantRowDTO
{
    matchId: number;
    userId: number;
    nicknameSnapshot: string;
    slotIndex: number;
    placement: number;
    livesLeft: number;
    expGained: number;
    scoreDelta: number;
}

/* DS → POST /api/match/result 계약 */

/** 결과 보고의 플레이어 1명. slotIndex·nickname은 백엔드가 roster에서 채운다. */
export interface MatchResultEntryInput
{
    userId: number;
    placement: number;   // 1=1등, 동점 허용
    livesLeft: number;
}

export interface MatchResultRequest
{
    matchId: string;     // 백엔드가 발급한 client_match_id(UUID)
    mapName: string;
    durationSec: number;
    endReason: MatchEndReason;
    results: MatchResultEntryInput[];
}

/** 응답: 확정된 각 플레이어의 점수 변화. */
export interface MatchResultParticipantDTO
{
    userId: number;
    placement: number;
    scoreDelta: number;
    scoreAfter: number;
}

export interface MatchResultResponse
{
    matchId: string;
    participants: MatchResultParticipantDTO[];
}
