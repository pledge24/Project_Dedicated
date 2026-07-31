// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1ExplosionHazard.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Game/Character/D1BomberCharacter.h"
#include "Game/D1BomberGridLibrary.h"

AD1ExplosionHazard::AD1ExplosionHazard()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
}

void AD1ExplosionHazard::Initialize(const TArray<FIntPoint>& InCells, float InDurationSec)
{
	if (!HasAuthority())
	{
		return;
	}

	HazardCells = InCells;
	HazardDurationSec = InDurationSec;
	ElapsedSec = 0.f;

	// 일정 간격마다(t=0 포함) 데미지 적용 로직 실행
	ApplyExplosionDamage();
	GetWorldTimerManager().SetTimer(HazardSweepTimerHandle, this, &AD1ExplosionHazard::TickHazard, HazardSweepInterval, true);
}

void AD1ExplosionHazard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AD1ExplosionHazard::TickHazard()
{
	ElapsedSec += HazardSweepInterval;
	ApplyExplosionDamage();

	if (ElapsedSec >= HazardDurationSec)
	{
		Destroy();
	}
}

void AD1ExplosionHazard::ApplyExplosionDamage()
{
	for (const FIntPoint& Cell : HazardCells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, UD1BomberGridLibrary::CellHalf);
		TArray<AD1BomberCharacter*> Chars;
		UD1BomberGridLibrary::OverlapBomberCharacters(this, Center, ExplosionHitExtent, Chars);

		for (AD1BomberCharacter* BC : Chars)
		{
			// 데미지 이벤트 전달.
			BC->ReceiveExplosionHit();
		}
	}
}
