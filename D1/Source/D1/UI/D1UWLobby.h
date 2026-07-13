// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "Network/BackendTypes.h"
#include "D1UWLobby.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;
class UUserWidget;
class UVerticalBox;

/**
 *  로비 위젯.
 *  로그인된 유저의 닉네임/점수 표시. 매칭 버튼은 이번 슬라이스에서 비활성.
 *  비로그인 상태로 들어오면 MP_Frontend로 강제 복귀 (방어 코드).
 *  자식 위젯 디자인 미완 — Bind는 일단 모두 Optional.
 */
UCLASS()
class UD1UWLobby : public UD1UserWidget
{
	GENERATED_BODY()

protected:
	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget Interface

//~ 프로필 표시
protected:
	/** 프로필 갱신(/api/auth/me 완료) 구독. */
	UFUNCTION()
	void HandleProfileUpdated();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NicknameLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ExpBar;

	/** EXP 바 중앙 텍스트("현재/최대 EXP"). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ExpLabel;

private:
	/** GameInstance 캐시(GetCurrentUser)의 닉네임/레벨/점수/경험치를 라벨·바에 반영. 캐시·갱신 양쪽에서 호출. */
	void ApplyProfileToLabels();

//~ WS 매칭
protected:
	UFUNCTION()
	void OnStartMatchingClicked();

	UFUNCTION()
	void OnCancelMatchingClicked();

	/** MatchmakingSubsystem 멀티캐스트 구독 (큐 입장·성사·에러). */
	UFUNCTION()
	void HandleQueueJoined();

	UFUNCTION()
	void HandleMatchFound(const FMatchFoundDTO& Match);

	UFUNCTION()
	void HandleMatchmakingError(const FBackendResponse& Error);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartMatchingButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> MatchStatusPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelMatchingButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MatchStatusLabel;
	
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MatchSearchingElapsedLabel;

private:
	/** 1초 간격 타이머 콜백 — 경과 초 증가·라벨 갱신. */
	void UpdateMatchSearchingElapsed();

	void StopMatchSearchingElapsed();

	FTimerHandle MatchSearchingElapsedTimerHandle;

	int32 MatchSearchingElapsedSec = 0;

//~ 랭킹
protected:
	UFUNCTION()
	void OnRankingClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RankingButton;

private:
	/** 랭킹 팝업 위젯 클래스 — 디테일 패널에서 WBP_Ranking 지정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lobby", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> RankingWidgetClass;

	/** 열린 팝업 참조 — 중복 오픈 가드. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> RankingWidget;

//~ 비로그인 방어
private:
	/** 비로그인 시 복귀할 맵 — 디테일 패널에서 MP_Frontend 지정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lobby", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> FrontendMap;
};
