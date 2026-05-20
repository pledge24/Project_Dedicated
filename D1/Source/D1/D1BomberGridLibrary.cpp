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

bool UD1BomberGridLibrary::IsInsideGrid(const FIntPoint& Cell)
{
	return Cell.X >= 0 && Cell.X < GridWidth && Cell.Y >= 0 && Cell.Y < GridHeight;
}

void UD1BomberGridLibrary::EnumerateCrossCells(
	const AD1BomberGameState* GameState,
	const FIntPoint& Origin,
	int32 Range,
	TArray<FIntPoint>& OutCells)
{
	OutCells.Reset();

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
			if (!IsInsideGrid(Cell))
			{
				break;
			}
			if (GameState && GameState->IsWallCell(Cell))
			{
				break;
			}
			OutCells.Add(Cell);
		}
	}
}

void UD1BomberGridLibrary::BuildDefaultWallCells(TArray<FIntPoint>& OutWallCells)
{
	OutWallCells.Reset();

	// 외벽은 경계 셀(X=0/GridWidth-1, Y=0/GridHeight-1)에 위치.
	// MP_Test에 배치된 AD1WallBlock 액터들과 동일 좌표.
	for (int32 X = 0; X < GridWidth; ++X)
	{
		OutWallCells.Add(FIntPoint(X, 0));
		OutWallCells.Add(FIntPoint(X, GridHeight - 1));
	}
	for (int32 Y = 1; Y < GridHeight - 1; ++Y)
	{
		OutWallCells.Add(FIntPoint(0, Y));
		OutWallCells.Add(FIntPoint(GridWidth - 1, Y));
	}

	// 내부 기둥: 짝수 좌표 (X∈{2,4,6,8,10}, Y∈{2,4,6,8,10,12})
	for (int32 X = 2; X < GridWidth - 1; X += 2)
	{
		for (int32 Y = 2; Y < GridHeight - 1; Y += 2)
		{
			OutWallCells.Add(FIntPoint(X, Y));
		}
	}
}
