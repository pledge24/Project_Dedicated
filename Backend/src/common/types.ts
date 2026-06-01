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

export interface MatchRowDTO
{
    id: number;
    clientMatchId: string;
    mapName: string;
    startedAt: string;
    endedAt: string;
    durationSec: number;
    endReason: 'winner' | 'draw' | 'time_expired' | 'abort';
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
