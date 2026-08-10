// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Map/D1MapBuilder.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Systems/Map/D1MapData.h"
#include "Game/D1PowerupPickup.h"
#include "Game/D1SoftBlock.h"
#include "Game/D1WallBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"

namespace
{
	// 확률·가중치로 블록 1개의 보유 아이템을 추첨. 드롭 없으면 false.
	bool RollPowerupType(const FD1MapBuildConfig& Cfg, EPowerupType& OutType)
	{
		if (FMath::FRand() > Cfg.DropChance)
		{
			return false;
		}

		const int32 TotalWeight = Cfg.FireWeight + Cfg.BombWeight + Cfg.SpeedWeight;
		if (TotalWeight <= 0)
		{
			return false;
		}

		// 종류가 3개뿐이라 if-else로 충분. 늘어나면 lower-bound 방식 고려.
		const int32 Roll = FMath::RandRange(0, TotalWeight - 1);
		if (Roll < Cfg.FireWeight)
		{
			OutType = EPowerupType::Fire;
		}
		else if (Roll < Cfg.FireWeight + Cfg.BombWeight)
		{
			OutType = EPowerupType::Bomb;
		}
		else
		{
			OutType = EPowerupType::Speed;
		}
		return true;
	}
}

bool UD1MapBuilder::Build(UWorld* World, AD1BomberGameState* GS, const FD1MapBuildConfig& Cfg, FString& OutError)
{
	if (!World || !GS)
	{
		OutError = TEXT("World/GameState 없음");
		return false;
	}

	// 1. 맵 선택: -MapData= 커맨드라인 오버라이드 우선(백엔드 주입/맵 스왑), 없으면 기본맵 사용.
	const UD1MapData* MapToUse = Cfg.DefaultMap;
	FString MapPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("MapData="), MapPath) && !MapPath.IsEmpty())
	{
		if (UD1MapData* Loaded = LoadObject<UD1MapData>(nullptr, *MapPath))
		{
			MapToUse = Loaded;
			UE_LOG(LogD1, Log, TEXT("[Map] -MapData= 오버라이드: %s"), *MapPath);
		}
		else
		{
			UE_LOG(LogD1, Warning, TEXT("[Map] -MapData= 로드 실패: %s — 기본값 사용"), *MapPath);
		}
	}

	if (!MapToUse)
	{
		OutError = TEXT("MapData 없음");
		return false;
	}

	// 2. 맵 레이아웃 생성
	FD1MapLayout Layout;
	FString ParseErr;
	if (!MapToUse->BuildLayout(Layout, ParseErr))
	{
		OutError = FString::Printf(TEXT("파싱 실패: %s"), *ParseErr);
		return false;
	}

	// 폭발/경계 판정 권위 데이터(클라에도 복제). 맵 논리 이름은 결과 보고용.
	GS->SetGridData(Layout.GridSize, Layout.WallCells, Layout.SoftBlockCells,
		MapToUse->GetEffectiveMapName());

	// 3. 맵 스폰
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 벽(복제) — 메시·콜리전은 액터 생성자에 있어 클라도 동일 구성.
	// 클래스 미지정 시 셀 판정 데이터(위 SetGridData)만 남고 액터 없는 맵이 된다 — 무음 생략 금지.
	if (ensureMsgf(MapToUse->GetWallBlockClass() != nullptr, TEXT("[Map] WallBlockClass 미지정: %s"), *MapToUse->GetName()))
	{
		for (const FIntPoint& Cell : Layout.WallCells)
		{
			World->SpawnActor<AD1WallBlock>(MapToUse->GetWallBlockClass(),
				UD1BomberGridLibrary::CellToWorldCenter(Cell, Cfg.BlockZ), FRotator::ZeroRotator, Params);
		}
	}

	// 소프트블록(복제) — 파괴 중 상태는 자체 복제.
	// PickupClass 미지정이면 배정 자체를 걸러야 한다 — null 클래스로 배정하면 아래 "아이템 N" 로그가
	// 성공을 주장하고 실제 드롭 시점에만 조용히 사라진다.
	const bool bCanAssignItems = Cfg.PickupClass != nullptr;
	ensureMsgf(bCanAssignItems, TEXT("[Map] PickupClass 미지정 — 아이템 사전 배정 안 함 (GameMode의 PowerupPickupClass)"));
	int32 AssignedItems = 0;
	if (ensureMsgf(MapToUse->GetSoftBlockClass() != nullptr, TEXT("[Map] SoftBlockClass 미지정: %s"), *MapToUse->GetName()))
	{
		for (const FIntPoint& Cell : Layout.SoftBlockCells)
		{
			AD1SoftBlock* Block = World->SpawnActor<AD1SoftBlock>(MapToUse->GetSoftBlockClass(),
				UD1BomberGridLibrary::CellToWorldCenter(Cell, Cfg.BlockZ), FRotator::ZeroRotator, Params);
			if (Block)
			{
				EPowerupType HeldType;
				if (bCanAssignItems && RollPowerupType(Cfg, HeldType))
				{
					Block->SetHeldItem(HeldType, Cfg.PickupClass, Cfg.PowerupZ);
					++AssignedItems;
				}
			}
		}
	}

	// 스폰 지점(서버 전용) — PlayerStartTag=슬롯 → ChoosePlayerStart의 ResolveSlotFromTag가 이 태그로 슬롯을 라벨링.
	for (const FD1MapStart& Start : Layout.Starts)
	{
		const FVector Loc = UD1BomberGridLibrary::CellToWorldCenter(Start.Cell, 0.f);
		if (APlayerStart* PS = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), Loc, FRotator::ZeroRotator, Params))
		{
			PS->PlayerStartTag = FName(*FString::FromInt(Start.Slot));
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Map] 빌드 완료 — %dx%d, 벽 %d, 소프트 %d, 스폰 %d, 아이템 %d"),
		Layout.GridSize.X, Layout.GridSize.Y,
		Layout.WallCells.Num(), Layout.SoftBlockCells.Num(), Layout.Starts.Num(), AssignedItems);

	return true;
}
