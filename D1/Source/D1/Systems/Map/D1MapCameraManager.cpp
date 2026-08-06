// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Map/D1MapCameraManager.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

void AD1MapCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	AActor* MapCam = ResolveMapCamera();
	if (MapCam)
	{
		OutVT.Target = MapCam;
	}

	// Super가 태그 액터의 transform/FOV로 POV를 채운다.
	Super::UpdateViewTarget(OutVT, DeltaTime);

	// 태그 액터가 앵글/FOV의 권위 → 그 회전을 받아 거리만 GridSize로 재계산.
	if (bUseGridFraming && MapCam)
	{
		ApplyGridFraming(OutVT.POV);
	}
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

void AD1MapCameraManager::ApplyGridFraming(FMinimalViewInfo& POV) const
{
	const AD1BomberGameState* GS = GetWorld()->GetGameState<AD1BomberGameState>();
	if (!GS)
	{
		// 클라 접속 초기 GameState 복제 전 — 매 프레임 재호출이 자연 재시도.
		return;
	}

	const FIntPoint Grid = GS->GetGridSize();
	if (Grid.X <= 0 || Grid.Y <= 0)
	{
		// 아직 GridSize 복제 전 → 태그 액터 그대로(대체).
		return;
	}

	const float CellSize = UD1BomberGridLibrary::CellSize;
	const float HalfW = (Grid.X + 2 * FramingPaddingCells) * CellSize * 0.5f;
	const float HalfH = (Grid.Y + 2 * FramingPaddingCells) * CellSize * 0.5f;

	// 그리드 셀 범위(0..Grid*CellSize)의 한가운데, 바닥 평면.
	const FVector MapCenter(Grid.X * CellSize * 0.5f, Grid.Y * CellSize * 0.5f, 0.f);

	// 뷰포트 종횡비(W/H). 못 구하면 16:9 가정.
	float Aspect = 16.f / 9.f;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.Y > 0)
		{
			Aspect = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
		}
	}

	// 그리드 코너(중심 기준 ±HalfW, ±HalfH, 0)를 right/up에 투영해
	// 화면 가로/세로로 필요한 반경을 구한다(회전 각도에 무관).
	const FRotationMatrix RotM(POV.Rotation);
	const FVector Forward = RotM.GetUnitAxis(EAxis::X);
	const FVector Right = RotM.GetUnitAxis(EAxis::Y);
	const FVector Up = RotM.GetUnitAxis(EAxis::Z);

	const float HalfRight = HalfW * FMath::Abs(Right.X) + HalfH * FMath::Abs(Right.Y);
	const float HalfUp = HalfW * FMath::Abs(Up.X) + HalfH * FMath::Abs(Up.Y);

	// UE 저장 FOV는 수평 기준. 수평으로 맞추면 가로/세로 어느 쪽도 안 잘림(여유는 padding이 흡수).
	const float TanHalfFov = FMath::Tan(FMath::DegreesToRadians(POV.FOV * 0.5f));
	if (TanHalfFov <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float DistForWidth = HalfRight / TanHalfFov;
	const float DistForHeight = (HalfUp * Aspect) / TanHalfFov;
	const float Dist = FMath::Max(DistForWidth, DistForHeight);

	POV.Location = MapCenter - Forward * Dist;
}
