// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "D1MapCameraManager.generated.h"

/**
 *  매 프레임 ViewTarget을 MapCameraTag 태그가 붙은 레벨 CameraActor로 강제.
 *  Possession 경쟁으로 ViewTarget이 폰 카메라로 되돌아가는 걸 막음.
 *
 *  추가로 bUseGridFraming이면 태그 액터의 앵글/FOV는 그대로 두되,
 *  카메라 위치만 GameState의 GridSize에 맞춰 계산해 맵 전체가 화면에 담기도록 한다
 *  (맵마다 그리드 크기가 달라도 카메라 수동 재배치 불필요).
 */
UCLASS()
class AD1MapCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AD1MapCameraManager();

protected:
	//~ Begin APlayerCameraManager Interface
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;
	//~ End APlayerCameraManager Interface

private:
	AActor* ResolveMapCamera();

	/** 태그 액터의 회전·FOV는 유지하고, GridSize에 맞춰 POV.Location만 다시 잡는다. */
	void ApplyGridFraming(FMinimalViewInfo& POV) const;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber|Camera")
	FName MapCameraTag = TEXT("MapViewCamera");

	/** 그리드 가장자리 바깥으로 더 보여줄 여백(셀 단위). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber|Camera")
	int32 FramingPaddingCells = 1;

	/** 끄면 태그 액터를 그대로 사용(기존 동작). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber|Camera")
	bool bUseGridFraming = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedCamera;
};
