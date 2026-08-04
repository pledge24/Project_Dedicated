// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "Framework/D1MatchTypes.h"
#include "D1UWMatchResultRow.generated.h"

class UTextBlock;

/**
 *  결과 화면의 한 줄. 등수 + 닉네임 표시.
 */
UCLASS()
class UD1UWMatchResultRow : public UD1UserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MatchResult")
	void SetEntry(const FD1MatchResultEntry& Entry);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlacementLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NicknameLabel;
};
