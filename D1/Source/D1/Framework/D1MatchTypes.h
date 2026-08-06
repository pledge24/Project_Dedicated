// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1MatchTypes.generated.h"

/** 매치 최대 슬롯 수(4인 개인전). 유효 슬롯 인덱스 = 0..D1MaxPlayerSlots-1. */
inline constexpr int32 D1MaxPlayerSlots = 4;

/** 매치 종료 사유. DS가 백엔드 결과 POST의 endReason으로 변환해 보낸다. */
UENUM()
enum class EBomberEndReason : uint8
{
	/** 단독 생존자. */
	Winner,
	/** 전원 동시 사망. */
	Draw,
	/** 제한시간 만료. */
	TimeExpired
};

/**
 *  -Roster= 로 주입된 입장 토큰 → 권위 신원(userId·닉네임) 매핑. 좌석은 DS가 입장 시 랜덤 배정.
 *  GameMode(파싱)와 MatchFlow(미입장자 보정)가 공유하므로 여기 둔다 — MatchFlow가 GameMode 헤더를
 *  include하면 역방향 의존이 된다.
 */
struct FD1JoinEntry
{
	int64 UserId = 0;
	FString Nickname;
};

/**
 *  GameMode가 InitGameState에서 GameState 컴포넌트들에 주입하는 매치 설정 묶음.
 *  MatchFlow와 PlayerRemoval이 같은 아이덴티티(정원·MatchId·ServerToken)를 공유해 한 덩어리로 넘긴다.
 */
struct FD1MatchSetupParams
{
	/** 시작 정원(0/1=즉시 시작). */
	int32 ExpectedPlayerCount = 0;

	/** 결과 POST·kick 조회 인증값. 비면 PIE/standalone(백엔드 호출 전부 스킵). */
	FString MatchId;
	FString ServerToken;

	/** 시작 게이트 대기 상한. */
	float WaitForPlayersTimeoutSec = 20.f;

	/** 매치 종료 후 DS 강제 종료까지의 유예. */
	float ShutdownGraceSec = 30.f;

	/**
	 * -Roster= 로 온 휴먼 명단. 끝까지 입장하지 않은 유저를 결과에 채우려면 "와야 할 사람"을 알아야 한다.
	 * 봇은 -Bots= 로 따로 와 PlayerArray에 편입되므로 여기 없다.
	 */
	TArray<FD1JoinEntry> ExpectedRoster;
};

/** 매치 종료 시 한 플레이어의 최종 결과. GameState가 배열로 원자 복제 → 결과 UI 표시용(백엔드 전송은 FMatchResultPlayer). */
USTRUCT(BlueprintType)
struct FD1MatchResultEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	int32 Placement = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	int32 SlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	int32 LivesLeft = 0;
};

/** 게임중 탈주한 슬롯의 카드 표시용. GameState가 복제 → PS가 제거돼도 카드가 "탈주"를 매치 끝까지 유지. */
USTRUCT(BlueprintType)
struct FD1LeftPlayerCard
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	int32 SlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	FString Nickname;
};
