// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "D1BomberGridLibrary.generated.h"

class AD1BomberGameState;

/**
 *  봄버맨 맵 그리드 좌표 헬퍼.
 *  - 셀 크기: 100cm (1m)
 *  - 셀 (X, Y)의 월드 중심: (X*100+50, Y*100+50, 0)
 *  - 전체 그리드 (외벽 포함): 13 x 15
 *  - 플레이어블 영역: X∈[1,11], Y∈[1,13] (클래식 11x13 레이아웃)
 */
UCLASS()
class UD1BomberGridLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static constexpr float CellSize = 100.f;	// 1m
	static constexpr int32 GridWidth = 13;   // 플레이어블 11 + 외벽 2열 (X=0, X=12)
	static constexpr int32 GridHeight = 15;  // 플레이어블 13 + 외벽 2행 (Y=0, Y=14)

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FIntPoint WorldToCell(const FVector& WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FVector CellToWorldCenter(const FIntPoint& Cell, float ZOverride = 0.f);

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static bool IsInsideGrid(const FIntPoint& Cell);

	/** 4방향 Range칸씩 진행, 벽/그리드 끝에서 멈춤. 원점 셀은 제외. */
	static void EnumerateCrossCells(
		const AD1BomberGameState* GameState,
		const FIntPoint& Origin,
		int32 Range,
		TArray<FIntPoint>& OutCells);

	/** 클래식 봄버맨 레이아웃 벽 셀 (외벽 + 짝수 좌표 내부 기둥). */
	static void BuildDefaultWallCells(TArray<FIntPoint>& OutWallCells);
};
