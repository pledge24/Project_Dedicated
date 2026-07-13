// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BackendTypes.generated.h"

/** 백엔드 에러 코드. 서버 응답 envelope의 error.code를 매핑한 enum. */
UENUM(BlueprintType)
enum class EBackendErrorCode : uint8
{
	None,
	ValidationFailed,
	InvalidCredentials,
	DuplicateLoginId,
	DuplicateNickname,
	RateLimited,
	/** HTTP 자체 실패 (서버 다운 / DNS / 타임아웃). */
	NetworkError,
	InternalError,
	Unknown
};

/** 매칭 진행 상태. 위젯이 버튼/문구를 결정할 때 참조. */
UENUM(BlueprintType)
enum class EMatchmakingState : uint8
{
	/** 큐 밖. */
	Idle,
	/** WS 연결 시도 중. */
	Connecting,
	/** 큐 입장 완료, 상대 대기. */
	Queued,
	/** 매칭 성사. */
	Matched
};

/** 인증된 유저 정보 (토큰 제외 — 토큰은 GameInstance가 별도 보관). */
USTRUCT(BlueprintType)
struct FAuthUserDTO
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Score = 1000;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Exp = 0;
};

/** 백엔드 응답 결과. 성공 여부 + 실패 시 코드/메시지. */
USTRUCT(BlueprintType)
struct FBackendResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	bool bOk = false;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	EBackendErrorCode ErrorCode = EBackendErrorCode::None;

	/** 서버가 내려준 사용자 메시지 (한글). 비어있으면 클라가 fallback. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString ErrorMessage;
};

/** 매칭 성사 정보. ServerHost/Port = 백엔드가 할당한 DS 주소(클라가 ?join= 으로 ClientTravel). */
USTRUCT(BlueprintType)
struct FMatchFoundDTO
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString MatchId;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString ServerHost;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 ServerPort = 0;

	/** 본인 입장 토큰. DS travel 시 ?join= 으로 제시(서버권위 신원). */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString JoinToken;
};

/** 랭킹 한 줄 — GET /api/ranking의 entries[] 한 항목. */
USTRUCT(BlueprintType)
struct FD1RankingEntryDTO
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Level = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Wins = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Losses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 MatchesPlayed = 0;
};

/** 랭킹 조회 결과 — Top 리스트 + 본인 순위(me.rank). 본인 닉네임/점수는 GameInstance 캐시서 합성. */
USTRUCT(BlueprintType)
struct FD1RankingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TArray<FD1RankingEntryDTO> Entries;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 MyRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Total = 0;
};

/** DS가 백엔드에 보고할 매치 결과 한 명분 (서버 내부용 — BP 비노출, USTRUCT 아님). */
struct FMatchResultPlayer
{
	int64 UserId = 0;
	int32 SlotIndex = 0;
	int32 Placement = 0;
	int32 LivesLeft = 0;
};

/** 회원가입/로그인 완료 콜백 (1회성 pass-in). */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAuthCompleted, const FBackendResponse&, Response, const FAuthUserDTO&, User);

/** 매칭 성사 푸시 (서버 발신 — 멀티캐스트). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchFound, const FMatchFoundDTO&, Match);

/** 큐 입장 확정 푸시. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQueueJoined);

/** 매칭 에러(연결 실패/거부/끊김). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchmakingError, const FBackendResponse&, Error);

/** 프로필 갱신 완료(/api/auth/me 응답으로 캐시 갱신됨). UI가 라벨 새로고침용으로 구독. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProfileUpdated);

/** 랭킹 조회 완료 콜백 (1회성 pass-in). */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnRankingCompleted, const FBackendResponse&, Response, const FD1RankingResult&, Result);
