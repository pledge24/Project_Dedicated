// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "D1MenuGameMode.generated.h"

class UUserWidget;

/**
 *  메뉴/로비 전용 GameMode.
 *  맵별 BP에서 InitialWidgetClass를 다르게 지정 (BP_MenuGameMode_Login → WBP_Login,
 *  BP_MenuGameMode_Lobby → WBP_Lobby).
 */
UCLASS(abstract)
class AD1MenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AD1MenuGameMode();

	TSubclassOf<UUserWidget> GetInitialWidgetClass() const { return InitialWidgetClass; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> InitialWidgetClass;
};
