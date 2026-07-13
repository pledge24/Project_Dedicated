// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "Network/BackendTypes.h"
#include "D1UWRanking.generated.h"

class UButton;
class UScrollBox;
class UD1UWRankingRow;

/**
 *  랭킹 팝업. 열릴 때 GET /api/ranking(Top 50)을 조회해 스크롤 목록을 채우고,
 *  하단에 본인 순위를 고정 표시한다. 우상단 CloseButton으로 닫음(RemoveFromParent).
 */
UCLASS()
class UD1UWRanking : public UD1UserWidget
{
	GENERATED_BODY()

protected:
	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	//~ End UUserWidget Interface

//~ 데이터 로딩
protected:
	/** FetchRanking 완료 콜백 — 목록·본인 행 채우기. */
	UFUNCTION()
	void HandleRankingCompleted(const FBackendResponse& Response, const FD1RankingResult& Result);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> RankingScrollBox;

	/** 스크롤 밖 하단 고정 본인 행(WBP_RankingRow 인스턴스). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UD1UWRankingRow> MyRankRow;

private:
	/** 항상 RankRowCount행 — 데이터 없는 뒤쪽은 빈 슬롯. 본인 행은 강조. */
	void PopulateRows(const FD1RankingResult& Result);

	/** 하단 고정 본인 행 = me.rank + GameInstance 캐시 닉네임/점수. */
	void ApplyMyRankRow(const FD1RankingResult& Result);

	/** 행 위젯 클래스 — 디테일 패널에서 WBP_RankingRow 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Ranking", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UD1UWRankingRow> RowWidgetClass;

	/** 리스트에 항상 채울 행 수(부족하면 빈 슬롯). */
	UPROPERTY(EditDefaultsOnly, Category = "Ranking", meta = (AllowPrivateAccess = "true"))
	int32 RankRowCount = 50;

//~ 닫기
protected:
	/** CloseButton(X) 클릭 — 팝업 제거. */
	UFUNCTION()
	void OnCloseClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;
};
