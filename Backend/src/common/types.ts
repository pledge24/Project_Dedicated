// 공유 도메인 타입·API 계약 정의. (단일 파일 전용 DB Row 타입은 각 repository에 콜로케이트)
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

export type MatchEndReason = 'winner' | 'draw' | 'time_expired' | 'abort';

/* DS → POST /api/match/result 계약 */

/** 결과 보고의 플레이어 1명. slotIndex(좌석)는 DS가 배정해 보고, nickname은 백엔드가 roster에서 채운다. */
export interface MatchResultEntryInput
{
    userId: number;
    slotIndex: number;   // DS가 배정한 좌석 0~3
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

/* GET /api/ranking 계약 */

/** 랭킹 한 줄 (응답 DTO). rank는 offset+index+1 위치 순위(동점은 user_id로 결정). */
export interface RankingEntry
{
    rank: number;
    userId: number;
    nickname: string;
    score: number;
    level: number;
    wins: number;
    losses: number;
    matchesPlayed: number;
}

/** 요청자 본인 정보. rank = 자기보다 점수 높은 인원 + 1 (동점은 공동 순위, 페이지와 무관한 전역 순위). */
export interface RankingSelf
{
    rank: number;
}

/** 응답: 페이지 + 본인 순위(me) + meta(클라 페이지네이션 렌더용). */
export interface RankingResponse
{
    entries: RankingEntry[];
    me: RankingSelf;
    meta: { total: number; limit: number; offset: number };
}
