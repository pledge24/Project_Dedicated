// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1ExplosionFX.generated.h"

class UStaticMeshComponent;

/**
 *  Short-lived visual effect actor for a single explosion cell.
 *  Spawned client-side via NetMulticast from AD1Bomb on explode.
 *  Expands quickly then destroys itself.
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
