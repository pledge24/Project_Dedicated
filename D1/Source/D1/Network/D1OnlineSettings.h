// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "D1OnlineSettings.generated.h"

/**
 *  D1 온라인 설정.
 *  Project Settings > D1 > Online 에 노출. 값은 Config/DefaultGame.ini 에 영속화.
 *  런타임에서는 GetDefault<UD1OnlineSettings>()->BaseUrl 형태로 읽는다.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="D1 Online"))
class UD1OnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("D1"); }
	//~ End UDeveloperSettings Interface

	UPROPERTY(Config, EditAnywhere, Category="Backend", meta=(DisplayName="Base URL"))
	FString BaseUrl = TEXT("http://127.0.0.1:3000");
};
