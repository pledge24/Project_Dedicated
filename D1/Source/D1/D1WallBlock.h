// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1WallBlock.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class AD1WallBlock : public AActor
{
	GENERATED_BODY()

public:
	AD1WallBlock();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* MeshComp;
};
