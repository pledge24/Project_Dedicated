// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1MapCameraManager.h"
#include "Kismet/GameplayStatics.h"

AD1MapCameraManager::AD1MapCameraManager()
{
	// Defaults inherited from APlayerCameraManager are fine.
}

void AD1MapCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	if (AActor* MapCam = ResolveMapCamera())
	{
		OutVT.Target = MapCam;
	}

	Super::UpdateViewTarget(OutVT, DeltaTime);
}

AActor* AD1MapCameraManager::ResolveMapCamera()
{
	if (CachedCamera.IsValid())
	{
		return CachedCamera.Get();
	}

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(this, MapCameraTag, Found);
	if (Found.Num() > 0)
	{
		CachedCamera = Found[0];
		return Found[0];
	}
	return nullptr;
}
