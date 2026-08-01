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
