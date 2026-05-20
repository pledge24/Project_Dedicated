// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "D1BomberGridLibrary.generated.h"

class AD1BomberGameState;

/**
 *  Grid coordinate helpers for the Bomberman map.
 *  - Cell size: 100 cm (1 m)
 *  - Cell (X, Y) world center: (X * 100 + 50, Y * 100 + 50, 0)
 *  - Total grid (outer wall ring included): 13 x 15
 *  - Playable area: cells X in [1, 11], Y in [1, 13]  (11 x 13 classic layout)
 */
UCLASS()
class UD1BomberGridLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static constexpr float CellSize = 100.f;	// 1m
	static constexpr int32 GridWidth = 13;   // 11 playable + 2 outer-wall columns (X=0, X=12)
	static constexpr int32 GridHeight = 15;  // 13 playable + 2 outer-wall rows    (Y=0, Y=14)

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FIntPoint WorldToCell(const FVector& WorldLocation);

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static FVector CellToWorldCenter(const FIntPoint& Cell, float ZOverride = 0.f);

	UFUNCTION(BlueprintPure, Category = "Bomber|Grid")
	static bool IsInsideGrid(const FIntPoint& Cell);

	/** Walk 4 directions Range steps each, stop on wall or grid edge. Excludes the origin cell. */
	static void EnumerateCrossCells(
		const AD1BomberGameState* GameState,
		const FIntPoint& Origin,
		int32 Range,
		TArray<FIntPoint>& OutCells);

	/** Returns all wall cells for the default 13x11 classic Bomberman layout
	 *  (outer ring + even-coord interior pillars). */
	static void BuildDefaultWallCells(TArray<FIntPoint>& OutWallCells);
};
