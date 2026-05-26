// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "D1LobbyWidget.generated.h"

class UButton;
class UTextBlock;

/**
 *  로비 위젯.
 *  로그인된 유저의 닉네임/점수 표시. 매칭 버튼은 이번 슬라이스에서 비활성.
 *  비로그인 상태로 들어오면 MP_Frontend로 강제 복귀 (방어 코드).
 */
UCLASS()
class UD1LobbyWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	//~ UUserWidget
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextBlock_Nickname;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextBlock_Score;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_StartMatching;

	/** 비로그인 시 복귀할 맵 — 디테일 패널에서 MP_Frontend 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> FrontendMap;
};
