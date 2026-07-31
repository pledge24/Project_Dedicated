// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/Function.h"
#include "D1BomberGridLibrary.generated.h"

class AD1BomberCharacter;
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
	static void TraceExplosionCells(
		const AD1BomberGameState* GameState,
		const FIntPoint& Origin,
		int32 Range,
		TArray<FIntPoint>& OutCells,
		TArray<FIntPoint>& OutSoftBlockHits);

	/** Start에서 4-이웃 BFS로 IsGoal을 처음 만족하는 최근접 셀까지 최단경로.
	 *  Start는 항상 확장(자기 폭탄 위 가능), 이웃만 IsPassable로 필터. 성공 시 OutPath(Start 포함) 채우고 true. */
	static bool FindNearestReachable(
		const AD1BomberGameState* GameState,
		const FIntPoint& Start,
		TFunctionRef<bool(FIntPoint)> IsGoal,
		TFunctionRef<bool(FIntPoint)> IsPassable,
		TArray<FIntPoint>& OutPath);

	/** 박스 안의 봄버 캐릭터 수집(Pawn 오버랩 질의 공용화 — 폭탄/폭발 피격 판정용). */
	static void OverlapBomberCharacters(const UObject* WorldContext, const FVector& Center, const FVector& Extent, TArray<AD1BomberCharacter*>& OutChars);

	/** Cells에 포함된 셀 위의 T 액터 순회 — "월드 전수 → 셀 변환 → 포함 검사" 패턴 공용화. */
	template <typename T>
	static void ForEachActorInCells(UWorld* World, const TArray<FIntPoint>& Cells, TFunctionRef<void(T*)> Visit)
	{
		if (!World || Cells.Num() == 0)
		{
			return;
		}

		for (T* Actor : TActorRange<T>(World))
		{
			if (IsValid(Actor) && Cells.Contains(WorldToCell(Actor->GetActorLocation())))
			{
				Visit(Actor);
			}
		}
	}

	static constexpr float CellSize = 100.f;
	/** 셀 중심 높이·블록 반폭·폭탄칸 풋프린트 공용(=50). */
	static constexpr float CellHalf = CellSize * 0.5f;
};
