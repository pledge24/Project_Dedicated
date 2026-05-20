// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1Bomb.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class AD1BomberPlayerState;

UCLASS()
class AD1Bomb : public AActor
{
	GENERATED_BODY()

public:
	AD1Bomb();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 전용: 폭탄 소유자 연결. */
	void Initialize(AD1BomberPlayerState* InOwner);

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	int32 Range;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float FuseSeconds;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	TObjectPtr<AD1BomberPlayerState> OwningPlayerState;

	UPROPERTY(ReplicatedUsing = OnRep_DetonationServerTime, BlueprintReadOnly, Category = "Bomber")
	float DetonationServerTime;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	virtual void BeginPlay() override;

	void DoExplode();

	/** 서버 전용: 다른 폭탄에 휘말렸을 때 거의 즉시 폭발하도록 예약. */
	void TriggerChainDetonation();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnExploded(const TArray<FIntPoint>& AffectedCells);

	UFUNCTION()
	void OnRep_DetonationServerTime();

private:
	FTimerHandle FuseTimerHandle;

	bool bIsExploding = false;
	bool bChainScheduled = false;
};
