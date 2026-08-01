// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1BomberGridLibrary.h"
#include "Algo/Reverse.h"
#include "Framework/D1BomberGameState.h"
#include "Game/Character/D1BomberCharacter.h"
#include "Kismet/KismetSystemLibrary.h"

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

void UD1BomberGridLibrary::TraceExplosionCells(
	const AD1BomberGameState* GameState,
	const FIntPoint& Origin,
	int32 Range,
	TArray<FIntPoint>& OutCells,
	TArray<FIntPoint>& OutSoftBlockHits)
{
	OutCells.Reset();
	OutSoftBlockHits.Reset();

	if (!GameState)
	{
		return;	
	}
	
	OutCells.Insert(Origin, 0);

	for (const FIntPoint& Dir : NeighborDirs)
	{
		for (int32 Step = 1; Step <= Range; ++Step)
		{
			const FIntPoint Cell = Origin + Dir * Step;
			if (!GameState->IsInsideGrid(Cell))
			{
				break;
			}
			if (GameState->IsWallCell(Cell))
			{
				// 영구벽: 셀 미포함, 즉시 정지.
				break;
			}
			if (GameState->IsSoftBlockCell(Cell))
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

bool UD1BomberGridLibrary::FindNearestReachable(
	const AD1BomberGameState* GameState,
	const FIntPoint& Start,
	TFunctionRef<bool(FIntPoint)> IsGoal,
	TFunctionRef<bool(FIntPoint)> IsPassable,
	TArray<FIntPoint>& OutPath)
{
	OutPath.Reset();

	if (!GameState)
	{
		return false;
	}

	// TArray + Head 인덱스로 FIFO 큐 대용(거리순 확장 → 첫 goal이 최근접).
	TArray<FIntPoint> Frontier;
	Frontier.Add(Start);
	TSet<FIntPoint> Visited;
	Visited.Add(Start);
	TMap<FIntPoint, FIntPoint> CameFrom;

	int32 Head = 0;
	FIntPoint GoalCell = Start;
	bool bFound = false;

	while (Head < Frontier.Num())
	{
		const FIntPoint Cur = Frontier[Head++];
		if (IsGoal(Cur))
		{
			GoalCell = Cur;
			bFound = true;
			break;
		}

		for (const FIntPoint& Dir : NeighborDirs)
		{
			const FIntPoint Next = Cur + Dir;
			if (Visited.Contains(Next))
			{
				continue;
			}
			// Start는 무조건 확장하되(위 초기화), 이웃은 통과 가능성으로 필터.
			if (!IsPassable(Next))
			{
				continue;
			}
			Visited.Add(Next);
			CameFrom.Add(Next, Cur);
			Frontier.Add(Next);
		}
	}

	if (!bFound)
	{
		return false;
	}

	// Goal → Start 역추적 후 뒤집어 Start 포함 정방향 경로로.
	FIntPoint Node = GoalCell;
	OutPath.Add(Node);
	while (Node != Start)
	{
		Node = CameFrom[Node];
		OutPath.Add(Node);
	}
	Algo::Reverse(OutPath);
	return true;
}

void UD1BomberGridLibrary::OverlapBomberCharacters(const UObject* WorldContext, const FVector& Center, const FVector& Extent, TArray<AD1BomberCharacter*>& OutChars)
{
	OutChars.Reset();

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> Found;
	UKismetSystemLibrary::BoxOverlapActors(WorldContext, Center, Extent, ObjectTypes,
		AD1BomberCharacter::StaticClass(), TArray<AActor*>(), Found);

	for (AActor* A : Found)
	{
		if (AD1BomberCharacter* BC = Cast<AD1BomberCharacter>(A))
		{
			OutChars.Add(BC);
		}
	}
}
