// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1MatchTypes.generated.h"

/** 매치 종료 시 한 플레이어의 최종 결과. GameState가 배열로 원자 복제 → UI 표시 + (추후) 백엔드 전송 공용. */
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
