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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

private:
	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float Lifetime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float ExpansionTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float PeakScale = 0.9f;

	/** 폭발 시작·소멸 시 스케일(작게 시작→PeakScale→작게). */
	UPROPERTY(EditDefaultsOnly, Category = "FX")
	float InitialScale = 0.05f;

	float Elapsed = 0.f;
};
