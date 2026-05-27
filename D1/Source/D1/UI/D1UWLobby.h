// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1UWLobby.generated.h"

class UButton;
class UTextBlock;

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
	//~ UUserWidget
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NicknameLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartMatchingButton;

private:
	/** 비로그인 시 복귀할 맵 — 디테일 패널에서 MP_Frontend 지정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Lobby", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> FrontendMap;
};
