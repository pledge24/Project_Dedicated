// roster 도메인 타입 — service와 repository가 공유한다.
// (별도 파일인 이유: repository가 service를 타입 때문에 역참조하면 모듈 그래프에 순환이 생긴다.)

/** 명단에 저장된 플레이어 데이터 단위 */
export interface RosterPlayer
{
    userId: number;
    nickname: string;
    joinToken: string;      // 매치별 1회용 입장 토큰. DS가 ?join= 으로 받은 토큰을 이 신원에 매핑. 봇은 빈 문자열.
    bot?: boolean;          // 봇전 봇 좌석(DB 미존재). true면 결과 저장 시 프로필/participants 기록 skip.
    rating?: number;        // 봇 ELO 입력 점수(백엔드 소유). 봇에만 존재.
}

/** 매치된 게임의 플레이어 명단(roster) */
export interface MatchRoster
{
    matchId: string;
    serverToken: string;
    mapName: string;
    startedAt: number;      // epoch ms — 매치 "성사" 시각(시작 시각 아님)
    /** DS가 시작 게이트를 통과한 시각(epoch ms). 미시작이면 undefined — 재입장 허용 여부를 가른다. */
    playStartedAt?: number;
    players: RosterPlayer[];
    /** 이 매치를 실행 중인 DS 주소. 재입장 안내에 쓴다. DB에 주소가 없는 행(server_host/port NULL)은 undefined. */
    server?: { host: string; port: number };
}
