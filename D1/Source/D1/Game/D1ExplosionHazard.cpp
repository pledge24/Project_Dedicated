// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1ExplosionHazard.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

#include "Game/Character/D1BomberCharacter.h"
#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberPlayerState.h"
#include "Game/D1BomberGridLibrary.h"

AD1ExplosionHazard::AD1ExplosionHazard()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;

	// 로직 전용 액터 — 메시·충돌 컴포넌트 없이 BoxOverlapActors 질의만 사용.
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

	// 즉시 1차 스윕(= 격발 순간 피해), 이후 불꽃 수명 동안 반복 스윕.
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
	AD1BomberGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AD1BomberGameMode>() : nullptr;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	// 같은 스윕 내 중복 피격 방지(스윕 간 중복은 피해자의 2초 무적이 차단).
	TSet<AD1BomberCharacter*> AlreadyHit;
	for (const FIntPoint& Cell : HazardCells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, UD1BomberGridLibrary::CellHalf);
		TArray<AActor*> Found;
		UKismetSystemLibrary::BoxOverlapActors(this, Center,
			ExplosionHitExtent,
			ObjectTypes,
			AD1BomberCharacter::StaticClass(),
			TArray<AActor*>(),
			Found);

		for (AActor* Hit : Found)
		{
			AD1BomberCharacter* BC = Cast<AD1BomberCharacter>(Hit);
			if (!BC || AlreadyHit.Contains(BC))
			{
				continue;
			}
			AlreadyHit.Add(BC);

			// 무적·생명·사망·경직은 전부 피해자가 결정. 공격자는 반환값으로 매치 통보만.
			if (BC->ReceiveExplosionHit() && GM)
			{
				GM->NotifyPlayerDied(BC->GetPlayerState<AD1BomberPlayerState>());
			}
		}
	}
}
