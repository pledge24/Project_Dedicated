// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "D1BomberGameState.generated.h"

class APlayerState;
class AD1BomberPlayerState;

UENUM(BlueprintType)
enum class EBomberMatchPhase : uint8
{
	Waiting   UMETA(DisplayName = "Waiting"),
	Playing   UMETA(DisplayName = "Playing"),
	Finished  UMETA(DisplayName = "Finished")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerCardsDirty);

UCLASS()
class AD1BomberGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AD1BomberGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsWallCell(const FIntPoint& Cell) const;

	/** 남은 매치 시간(초). 클라/서버 공용. UMG가 Tick에서 폴링용. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	float GetRemainingTimeSec() const;

	/** 슬롯 인덱스(0~3) 순서로 정렬된 PlayerState 배열. 빈 슬롯은 nullptr.
	 *  UI 카드 컨테이너가 ForEach로 돌면서 Cards[Index]에 바인딩하는 용도. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	TArray<AD1BomberPlayerState*> GetPlayerStatesBySlot() const;

	/** PS가 들어오고/나가고/슬롯 변경될 때 등 UI 카드 재바인딩이 필요한 모든 시점에 브로드캐스트.
	 *  PlayerCardContainer 위젯이 Construct에서 한 번만 구독하고, 콜백에서 전체 PlayerArray를 재스캔. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerCardsDirty OnPlayerCardsDirty;

	/** PlayerState 같은 외부에서 카드 갱신을 알릴 때 호출. UI 컨테이너가 Construct에서 초기 fire용으로도 사용. */
	UFUNCTION(BlueprintCallable, Category = "Bomber|Events")
	void MarkPlayerCardsDirty();

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

protected:
	UFUNCTION()
	void OnRep_MatchPhase();
};
