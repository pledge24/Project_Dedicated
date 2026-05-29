// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1MatchTypes.h"
#include "D1UWMatchResult.generated.h"

class UButton;
class UVerticalBox;
class UD1UWMatchResultRow;

/**
 *  매치 결과 화면. 등수 오름차순으로 행을 채운다.
 *  데이터는 GameState.FinalResults에서 PC가 넘겨준다(서버 권위).
 */
UCLASS()
class UD1UWMatchResult : public UD1UserWidget
{
	GENERATED_BODY()

public:
	/** 결과 배열을 등수순 정렬해 행 위젯으로 채운다. */
	UFUNCTION(BlueprintCallable, Category = "MatchResult")
	void SetResults(const TArray<FD1MatchResultEntry>& Results);

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ResultListPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton;

private:
	/** 행 위젯 클래스 — 디테일 패널에서 WBP_MatchResultRow 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "MatchResult", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UD1UWMatchResultRow> RowWidgetClass;
};
