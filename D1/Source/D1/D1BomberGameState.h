// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "D1MatchTypes.h"
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

	/**-------------------
	 *	  API Function
	 *-------------------*/
	
	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsWallCell(const FIntPoint& Cell) const;

	/** 파괴 가능 블록(soft block)이 아직 살아있는 셀인지. 폭발 전파가 이 셀까지 닿은 뒤 멈춘다. */
	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsSoftBlockCell(const FIntPoint& Cell) const;

	/** 셀이 그리드 범위 안인지(GridSize 기준). 맵마다 그리드 크기가 다르므로 GameState가 권위. */
	UFUNCTION(BlueprintPure, Category = "Bomber")
	bool IsInsideGrid(const FIntPoint& Cell) const;

	/** 남은 매치 시간(초). 클라/서버 공용. WBP_MatchTimer가 1초마다 호출. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	float GetRemainingTimeSec() const;

	/** 슬롯 인덱스(0~3) 순서로 정렬된 PlayerState 배열. 빈 슬롯은 nullptr.
	 *  UI 카드 컨테이너가 ForEach로 돌면서 Cards[Index]에 바인딩하는 용도. */
	UFUNCTION(BlueprintPure, Category = "Bomber|Match")
	TArray<AD1BomberPlayerState*> GetPlayerStatesBySlot() const;

	/** PlayerState 같은 외부에서 카드 갱신을 알릴 때 호출. UI 컨테이너가 Construct에서 초기 fire용으로도 사용. */
	UFUNCTION(BlueprintCallable, Category = "Bomber|Events")
	void MarkPlayerCardsDirty();

	/** 서버 전용: 최종 결과 스냅샷을 설정하고 OnMatchFinished를 알린다(리슨 서버 자기 클라 포함). */
	void SetFinalResults(const TArray<FD1MatchResultEntry>& InResults);

	/** 서버 전용: 파괴된 블록 셀을 목록에서 제거 → 이후 폭발이 그 셀을 통과. */
	void RemoveSoftBlockCell(const FIntPoint& Cell);

	/**-------------------
	 *	    API Data
	 *-------------------*/
	
	/** PS가 들어오고/나가고/슬롯 변경될 때 등 UI 카드 재바인딩이 필요한 모든 시점에 브로드캐스트.
	 *  PlayerCardContainer 위젯이 Construct에서 한 번만 구독하고, 콜백에서 전체 PlayerArray를 재스캔. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerCardsDirty OnPlayerCardsDirty;

	/** 매치 종료 + 결과 스냅샷 도착 시 1회 브로드캐스트. PC가 결과 위젯 표시용으로 구독. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnMatchFinished OnMatchFinished;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, BlueprintReadOnly, Category = "Bomber")
	EBomberMatchPhase MatchPhase;

	/** 맵 그리드 크기(열, 행). 빌드 시 서버가 데이터에서 세팅. 경계 판정의 권위. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	FIntPoint GridSize;

	/** 복제되는 벽 셀 목록. ~64개라 TArray + Contains로 충분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TArray<FIntPoint> WallCells;

	/** 복제되는 파괴 가능 블록 셀 목록. 폭발에 파괴되면 RemoveSoftBlockCell로 빠진다. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TArray<FIntPoint> SoftBlockCells;

	/** 매치 시작 서버 시각(초). GameMode가 Playing 진입 시 기록. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchStartServerTime;

	/** 매치 제한 시간(초). 기본 5분. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Match")
	float MatchDurationSec;

	/** 매치 종료 시 서버가 한 번 채우는 최종 결과(등수 포함). 단일 배열로 원자 복제. */
	UPROPERTY(ReplicatedUsing = OnRep_FinalResults, BlueprintReadOnly, Category = "Bomber|Match")
	TArray<FD1MatchResultEntry> FinalResults;

protected:
	UFUNCTION()
	void OnRep_MatchPhase();

	UFUNCTION()
	void OnRep_FinalResults();
};
