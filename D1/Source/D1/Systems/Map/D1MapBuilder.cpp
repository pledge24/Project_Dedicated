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

		// 드롭 확정.(아이템 종류가 3개 밖에 없어 if-else 방식 사용.
		// 종류가 많아지면 lower-bound 사용 고려 중)
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

	// 폭발/경계 판정 권위 데이터(클라에도 복제).
	GS->GridSize = Layout.GridSize;
	GS->WallCells = Layout.WallCells;
	GS->SoftBlockCells = Layout.SoftBlockCells;

	// 3. 맵 스폰
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 벽(복제) — 메시·콜리전은 액터 생성자에 있어 클라도 동일 구성.
	if (MapToUse->WallBlockClass)
	{
		for (const FIntPoint& Cell : Layout.WallCells)
		{
			World->SpawnActor<AD1WallBlock>(MapToUse->WallBlockClass,
				UD1BomberGridLibrary::CellToWorldCenter(Cell, Cfg.BlockZ), FRotator::ZeroRotator, Params);
		}
	}

	// 소프트블록(복제) — 파괴 중 상태는 자체 복제.
	int32 AssignedItems = 0;
	if (MapToUse->SoftBlockClass)
	{
		for (const FIntPoint& Cell : Layout.SoftBlockCells)
		{
			AD1SoftBlock* Block = World->SpawnActor<AD1SoftBlock>(MapToUse->SoftBlockClass,
				UD1BomberGridLibrary::CellToWorldCenter(Cell, Cfg.BlockZ), FRotator::ZeroRotator, Params);
			if (Block)
			{
				// 랜덤으로 아이템 채워넣기.
				EPowerupType HeldType;
				if (RollPowerupType(Cfg, HeldType))
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
