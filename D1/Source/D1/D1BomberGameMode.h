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

	/** 서버 전용: 플레이어 사망 등록, 등수 부여, 1명 남으면 매치 종료. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	bool bMatchEnded = false;

	void PopulateWallData();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS);
	void EnsureAliveListInitialized();
};
