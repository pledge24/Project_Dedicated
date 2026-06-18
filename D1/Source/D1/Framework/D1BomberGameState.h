// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/D1MatchTypes.h"
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMatchFinished);

UCLASS()
class AD1BomberGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AD1BomberGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	//~ 외부 API
	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsWallCell(const FIntPoint& Cell) const;

	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsSoftBlockCell(const FIntPoint& Cell) const;

	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsInsideGrid(const FIntPoint& Cell) const;

	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	float GetRemainingTimeSec() const;

	/** 슬롯 0~3 순 정렬, 빈 슬롯은 nullptr. UI 카드가 인덱스로 바인딩. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	TArray<AD1BomberPlayerState*> GetPlayerStatesBySlot() const;

	UFUNCTION(BlueprintCallable, Category = "Bomber|Events")
	void MarkPlayerCardsDirty();

	/** 서버 전용: 결과 스냅샷 설정 + OnMatchFinished 방송(리슨 서버 자기 클라 포함). */
	void SetFinalResults(const TArray<FD1MatchResultEntry>& InResults);

	/** 서버 전용: 파괴된 블록 셀 제거 → 이후 폭발이 통과. */
	void RemoveSoftBlockCell(const FIntPoint& Cell);

	//~ 이벤트
	/** UI 카드 재바인딩 필요 시점마다 방송. 컨테이너가 1회 구독 후 전체 재스캔. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerCardsDirty OnPlayerCardsDirty;

	/** 매치 종료+결과 도착 시 1회. PC가 결과 위젯용으로 구독. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnMatchFinished OnMatchFinished;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, BlueprintReadOnly, Category = "Bomber")
	EBomberMatchPhase MatchPhase;

	/** 빌드 시 서버가 세팅. 경계 판정 권위. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	FIntPoint GridSize;

	/** ~64셀 규모라 TArray로 충분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TArray<FIntPoint> WallCells;

	/** 폭발에 파괴되면 RemoveSoftBlockCell로 빠진다. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TArray<FIntPoint> SoftBlockCells;

	/** GameMode가 Playing 진입 시 기록. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchStartServerTime;

	/** 기본 5분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchDurationSec;

	/** 종료 시 서버가 1회 채움. 단일 배열로 원자 복제. */
	UPROPERTY(ReplicatedUsing = OnRep_FinalResults, BlueprintReadOnly, Category = "Bomber|Match")
	TArray<FD1MatchResultEntry> FinalResults;

protected:
	UFUNCTION()
	void OnRep_MatchPhase();

	UFUNCTION()
	void OnRep_FinalResults();
};
