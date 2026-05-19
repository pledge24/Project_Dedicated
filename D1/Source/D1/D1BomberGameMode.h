// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1GameMode.h"
#include "D1BomberGameMode.generated.h"

class AD1WallBlock;
class AD1BomberPlayerState;

UCLASS(abstract)
class AD1BomberGameMode : public AD1GameMode
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Server-only: register that a player died, assign placement, end match if 1 alive. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

	void PopulateWallData();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS);
};
