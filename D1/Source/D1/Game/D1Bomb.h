// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1Bomb.generated.h"

class AD1ExplosionFX;
class AD1ExplosionHazard;
class UBoxComponent;
class UStaticMeshComponent;

/** 폭탄 생애 단계(서버 전용 상태). */
enum class ED1BombState : uint8
{
	/** 도화선 카운트다운 중. */
	Fusing,
	/** 체인 격발 예약됨(짧은 지연 대기). */
	Detonating,
	/** 폭발 처리 시작됨(곧 Destroy). */
	Exploding,
};

/** 그리드 스냅 폭탄 — 도화선 카운트다운 후 십자 폭발, 다른 폭탄에 휘말리면 체인 격발. */
UCLASS()
class AD1Bomb : public AActor
{
	GENERATED_BODY()

public:
	AD1Bomb();

	//~ Begin AActor Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

protected:
	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface

//~ 컴포넌트
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

//~ 격발 타이밍
public:
	/** 서버 로컬 시각 기준 폭발 예정 시각. 봇 위험 회피의 잔여 도화선 계산용. */
	float GetDetonationServerTime() const { return DetonationServerTime; }

protected:
	UFUNCTION()
	void OnRep_DetonationServerTime();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float FuseSec = 3.f;

	UPROPERTY(ReplicatedUsing = OnRep_DetonationServerTime, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	float DetonationServerTime = 0.f;

	FTimerHandle FuseTimerHandle;

	ED1BombState State = ED1BombState::Fusing;

//~ 폭발 처리
public:
	/** 서버 전용: 설치자 화력으로 폭발 범위 덮어쓰기(스폰 직후). */
	void SetRange(int32 InRange);
	/** 폭발 반경(칸). 봇 위험셀 계산용. */
	int32 GetRange() const { return Range; }

protected:
	/** 폭발 이펙트 스폰 Multicast */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnExploded(const TArray<FIntPoint>& AffectedCells);

	/** 서버 전용. */
	void DoExplode();

private:
	void DestroySoftBlocks(const TArray<FIntPoint>& SoftBlockHits);
	/** 폭발 십자에 걸린 드롭 파워업 파괴. */
	void DestroyPowerups(const TArray<FIntPoint>& Cells);
	/** 서버 전용. */
	void SpawnExplosionHazard(const TArray<FIntPoint>& Cells);

	/** 폭발 셀마다 스폰하는 FX 액터. 미지정 시 C++ 클래스로 폴백. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1ExplosionFX> ExplosionFXClass;

	/** 폭발 지속 피해용 서버 전용 위험 액터. 미지정 시 C++ 클래스로 폴백. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1ExplosionHazard> ExplosionHazardClass;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	int32 Range = 2;

	/** 폭발 불꽃이 피해를 주는 지속 시간(초). 비주얼 AD1ExplosionFX::Lifetime 이하로 유지. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float ExplosionLingerDurationSec = 0.5f;

//~ 체인 폭발
protected:
	/** 서버 전용: 다른 폭탄에 휘말렸을 때 거의 즉시 폭발하도록 예약. */
	void TriggerChainDetonation();

private:
	void ChainDetonateBombs(const TArray<FIntPoint>& Cells);

	/** 다른 폭탄에 휘말렸을 때 체인 폭발까지의 지연(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float ChainDetonationDelay = 0.05f;
};
