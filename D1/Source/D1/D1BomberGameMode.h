// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1GameMode.h"
#include "D1BomberGameMode.generated.h"

UCLASS(abstract)
class AD1BomberGameMode : public AD1GameMode
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;
};
