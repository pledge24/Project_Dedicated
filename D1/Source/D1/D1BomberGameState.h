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

	/** 매치 시작 서버 시각(초). GameMode가 Playing 진입 시 기록. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchStartServerTime;

	/** 매치 제한 시간(초). 기본 5분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchDurationSec;

	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsWallCell(const FIntPoint& Cell) const;

	/** 남은 매치 시간(초). 클라/서버 공용. UMG가 Tick에서 폴링용. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	float GetRemainingTimeSec() const;

protected:
	UFUNCTION()
	void OnRep_MatchPhase();
};
