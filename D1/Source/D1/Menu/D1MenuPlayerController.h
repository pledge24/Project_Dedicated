// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "D1MenuPlayerController.generated.h"

class UUserWidget;

/**
 *  메뉴/로비용 PlayerController.
 *  BeginPlay에서 GameMode의 InitialWidgetClass로 위젯을 생성하고
 *  마우스 커서 ON + UI 전용 입력 모드로 전환한다.
 */
UCLASS(abstract)
class AD1MenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AD1MenuPlayerController();

	//~ APlayerController
	virtual void BeginPlay() override;

	/** 다른 위젯으로 교체 (로그인 ↔ 회원가입 등). 기존 위젯은 RemoveFromParent. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void SwitchToWidget(TSubclassOf<UUserWidget> NewWidgetClass);

private:
	void ShowInitialWidgetFromGameMode();
	void ApplyUiOnlyInputMode();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> CurrentWidget;
};
