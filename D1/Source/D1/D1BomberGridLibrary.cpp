// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGridLibrary.h"
#include "D1BomberGameState.h"

FIntPoint UD1BomberGridLibrary::WorldToCell(const FVector& WorldLocation)
{
	const int32 X = FMath::FloorToInt(WorldLocation.X / CellSize);
	const int32 Y = FMath::FloorToInt(WorldLocation.Y / CellSize);
	return FIntPoint(X, Y);
}

FVector UD1BomberGridLibrary::CellToWorldCenter(const FIntPoint& Cell, float ZOverride)
{
	return FVector(Cell.X * CellSize + CellSize * 0.5f,
	               Cell.Y * CellSize + CellSize * 0.5f,
	               ZOverride);
}

void UD1BomberGridLibrary::EnumerateCrossCells(
	const AD1BomberGameState* GameState,
	const FIntPoint& Origin,
	int32 Range,
	TArray<FIntPoint>& OutCells,
	TArray<FIntPoint>& OutSoftBlockHits)
{
	OutCells.Reset();
	OutSoftBlockHits.Reset();

	static const FIntPoint Directions[4] = {
		FIntPoint( 1,  0),
		FIntPoint(-1,  0),
		FIntPoint( 0,  1),
		FIntPoint( 0, -1)
	};

	for (const FIntPoint& Dir : Directions)
	{
		for (int32 Step = 1; Step <= Range; ++Step)
		{
			const FIntPoint Cell = Origin + Dir * Step;
			if (!GameState || !GameState->IsInsideGrid(Cell))
			{
				break;
			}
			if (GameState && GameState->IsWallCell(Cell))
			{
				// 영구벽: 셀 미포함, 즉시 정지.
				break;
			}
			if (GameState && GameState->IsSoftBlockCell(Cell))
			{
				// 파괴 가능 블록: FX/데미지 셀엔 미포함(줄기가 블록에 안 닿음).
				// 파괴 대상으로만 보고하고 정지 — 뒤 칸은 보호.
				OutSoftBlockHits.Add(Cell);
				break;
			}
			OutCells.Add(Cell);
		}
	}
}
