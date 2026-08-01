// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "Network/BackendTypes.h"
#include "D1UWRankingRow.generated.h"

class UBorder;
class UTextBlock;

/**
 *  랭킹 목록의 한 줄. 순위 + 닉네임 + 점수 표시.
 */
UCLASS()
class UD1UWRankingRow : public UD1UserWidget
{
	GENERATED_BODY()

public:
	/** 랭킹 한 항목을 행에 바인딩. bIsLocalPlayer면 RowBorder를 강조색으로 칠한다. */
	UFUNCTION(BlueprintCallable, Category = "Ranking")
	void SetEntry(const FD1RankingEntryDTO& Entry, bool bIsLocalPlayer);

	/** 데이터 없는 빈 슬롯 — 순위 번호만, 닉네임/점수는 "-". */
	UFUNCTION(BlueprintCallable, Category = "Ranking")
	void SetEmpty(int32 Rank);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RowBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RankLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NicknameLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreLabel;

private:
	/** 본인 행 배경 강조색(반투명 오렌지). 비본인/빈 슬롯은 투명. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranking", meta = (AllowPrivateAccess = "true"))
	FLinearColor LocalHighlightColor = FLinearColor(1.f, 0.72f, 0.42f, 0.25f);
};
