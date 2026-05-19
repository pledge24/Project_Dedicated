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

	// Outer ring sits one cell OUTSIDE the playable 13x11 grid so the entire
	// floor area is walkable. Includes corner cells.
	for (int32 X = -1; X <= GridWidth; ++X)
	{
		OutWallCells.Add(FIntPoint(X, -1));
		OutWallCells.Add(FIntPoint(X, GridHeight));
	}
	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		OutWallCells.Add(FIntPoint(-1, Y));
		OutWallCells.Add(FIntPoint(GridWidth, Y));
	}

	// Interior odd-coord pillars: x in {1,3,5,7,9,11}, y in {1,3,5,7,9}
	for (int32 X = 1; X < GridWidth; X += 2)
	{
		for (int32 Y = 1; Y < GridHeight; Y += 2)
		{
			OutWallCells.Add(FIntPoint(X, Y));
		}
	}
}
