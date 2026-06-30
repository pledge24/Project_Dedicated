// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "Framework/D1MatchTypes.h"
#include "D1UWMatchResult.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UD1UWMatchResultRow;

/**
 *  매치 결과 화면. 등수 오름차순으로 행을 채운다.
 *  데이터는 GameState.FinalResults에서 PC가 넘겨준다(서버 권위).
 *  카운트다운 종료 또는 LeaveButton 클릭 시 MP_Lobby로 복귀.
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
	virtual void NativeConstruct() override;

	/** LeaveButton 클릭 — 즉시 로비로. */
	UFUNCTION()
	void OnLeaveClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ResultListPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton;

	/** 자동 복귀까지 남은 초 표시(선택). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountdownLabel;

private:
	/** 1초마다 카운트다운 감소·라벨 갱신, 0이면 복귀. */
	void OnCountdownTick();

	/** DS 연결을 끊고 MP_Lobby로 Travel. 중복 호출 가드. */
	void ReturnToLobby();

	/** 행 위젯 클래스 — 디테일 패널에서 WBP_MatchResultRow 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "MatchResult", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UD1UWMatchResultRow> RowWidgetClass;

	/** 복귀할 로비 맵 — 디테일 패널에서 MP_Lobby 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "MatchResult", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> LobbyMap;

	/** 결과 표시 후 자동 복귀까지의 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "MatchResult", meta = (AllowPrivateAccess = "true"))
	float ReturnCountdownSec = 15.f;

	FTimerHandle CountdownTimerHandle;

	int32 RemainingSec = 0;

	bool bReturning = false;
};
