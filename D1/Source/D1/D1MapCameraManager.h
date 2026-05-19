// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "D1MapCameraManager.generated.h"

/**
 *  Forces ViewTarget to the level-placed CameraActor tagged with MapCameraTag,
 *  every frame. Bypasses possession races that would otherwise revert ViewTarget
 *  to the pawn's camera component.
 */
UCLASS()
class AD1MapCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AD1MapCameraManager();

	UPROPERTY(EditDefaultsOnly, Category = "Bomber|Camera")
	FName MapCameraTag = TEXT("MapViewCamera");

protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedCamera;

	AActor* ResolveMapCamera();
};
