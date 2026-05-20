// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1ExplosionFX.generated.h"

class UStaticMeshComponent;

/**
 *  한 폭발 셀용 단기 비주얼 액터.
 *  AD1Bomb의 NetMulticast로 클라 측에서 스폰.
 *  빠르게 부풀었다가 자동 소멸.
 */
UCLASS()
class AD1ExplosionFX : public AActor
{
	GENERATED_BODY()

public:
	AD1ExplosionFX();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float Lifetime;

	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float ExpansionTime;

	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float PeakScale;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

private:
	float Elapsed;
};
