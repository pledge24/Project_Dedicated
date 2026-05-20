// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "D1MapCameraManager.generated.h"

/**
 *  매 프레임 ViewTarget을 MapCameraTag 태그가 붙은 레벨 CameraActor로 강제.
 *  Possession 경쟁으로 ViewTarget이 폰 카메라로 되돌아가는 걸 막음.
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
