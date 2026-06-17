// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "D1BomberGridLibrary.generated.h"

class AD1BomberGameState;

/**
 *  봄버맨 맵 그리드 좌표 헬퍼.
 *  - 셀 크기: 100cm (1m) 고정.
 *  - 셀 (X, Y)의 월드 중심: (X*100+50, Y*100+50, 0)
 *  - 그리드 "크기"는 맵마다 다름 → 경계 판정은 AD1BomberGameState::IsInsideGrid(GridSize) 권위.
 */
UCLASS()
class UD1BomberGridLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FIntPoint WorldToCell(const FVector& WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FVector CellToWorldCenter(const FIntPoint& Cell, float ZOverride = 0.f);

	/** 4방향 Range칸 진행, 벽/그리드 끝에서 멈춤(원점 제외).
	 *  파괴 블록 셀은 OutCells에서 빼고 OutSoftBlockHits로 보고 후 정지. */
	static void EnumerateCrossCells(
		const AD1BomberGameState* GameState,
		const FIntPoint& Origin,
		int32 Range,
		TArray<FIntPoint>& OutCells,
		TArray<FIntPoint>& OutSoftBlockHits);

	static constexpr float CellSize = 100.f;
	/** 셀 중심 높이·블록 반폭·폭탄칸 풋프린트 공용(=50). */
	static constexpr float CellHalf = CellSize * 0.5f;
};
