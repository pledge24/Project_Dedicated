// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1Bomb.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class AD1Bomb : public AActor
{
	GENERATED_BODY()

public:
	AD1Bomb();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 전용: 설치자 화력으로 폭발 범위 덮어쓰기(스폰 직후). */
	void SetRange(int32 InRange);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnExploded(const TArray<FIntPoint>& AffectedCells);

	UFUNCTION()
	void OnRep_DetonationServerTime();

	void DoExplode();

	/** 서버 전용: 다른 폭탄에 휘말렸을 때 거의 즉시 폭발하도록 예약. */
	void TriggerChainDetonation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

private:
	void ChainDetonateBombs(const TArray<FIntPoint>& Cells);
	void DestroySoftBlocks(const TArray<FIntPoint>& SoftBlockHits);
	void ApplyExplosionDamage(const TArray<FIntPoint>& Cells);

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	int32 Range;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float FuseSeconds;

	/** 다른 폭탄에 휘말렸을 때 체인 폭발까지의 지연(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float ChainDetonationDelay = 0.05f;

	UPROPERTY(ReplicatedUsing = OnRep_DetonationServerTime, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	float DetonationServerTime;

	FTimerHandle FuseTimerHandle;

	bool bIsExploding = false;
	bool bChainScheduled = false;
};
