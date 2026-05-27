// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "D1MenuPlayerController.generated.h"

class UUserWidget;

/**
 *  메뉴/로비용 PlayerController.
 *  BeginPlay에서 BackgroundWidget(고정 배경)을 깐 뒤
 *  GameMode의 InitialWidgetClass로 콘텐츠 위젯을 그 위에 띄운다.
 *  콘텐츠는 SwitchToWidget으로 갈아끼우고, 배경은 그대로 유지된다.
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
	void ShowBackground();
	void ApplyUiOnlyInputMode();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu",
	          meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> BackgroundWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> CurrentWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> BackgroundWidget;
};
