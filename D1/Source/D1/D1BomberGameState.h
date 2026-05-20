// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "D1BomberGameState.generated.h"

UENUM(BlueprintType)
enum class EBomberMatchPhase : uint8
{
	Waiting   UMETA(DisplayName = "Waiting"),
	Playing   UMETA(DisplayName = "Playing"),
	Finished  UMETA(DisplayName = "Finished")
};

UCLASS()
class AD1BomberGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AD1BomberGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, BlueprintReadOnly, Category = "Bomber")
	EBomberMatchPhase MatchPhase;

	/** 복제되는 벽 셀 목록. ~64개라 TArray + Contains로 충분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TArray<FIntPoint> WallCells;

	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsWallCell(const FIntPoint& Cell) const;

protected:
	UFUNCTION()
	void OnRep_MatchPhase();
};
