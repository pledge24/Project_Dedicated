// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1Bomb.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

#include "Game/Character/D1BomberCharacter.h"
#include "Game/Character/D1BombPlacementComponent.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/D1ExplosionFX.h"
#include "Game/D1ExplosionHazard.h"
#include "Game/D1PowerupPickup.h"
#include "Game/D1SoftBlock.h"

AD1Bomb::AD1Bomb()
{
	bReplicates = true;
	
	// 제자리 고정 액터 — 이동 복제 불필요, 넷 갱신 10Hz(기본 100)면 충분, Tick 없음.
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	PrimaryActorTick.bCanEverTick = false;

	// 충돌 박스는 셀(100cm)보다 작은 80cm — 옆 칸 캐릭터가 폭탄 모서리에 끼는 것 방지.
	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
	CollisionComp->InitBoxExtent(FVector(40.f, 40.f, 40.f));
	CollisionComp->SetCollisionProfileName(TEXT("BomberBomb"));
	RootComponent = CollisionComp;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AD1Bomb::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, FuseSec, false);
	}

	// 폭탄이 스폰된 타이밍에 해당 셀 내부에 위치한 캐릭터들은 Sweep 충돌을 무시하도록 등록. (서버/클라 양쪽에서 실행)
	{
		TArray<AD1BomberCharacter*> Overlapping;
		UD1BomberGridLibrary::OverlapBomberCharacters(this, GetActorLocation(),
			FVector(UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellSize),
			Overlapping);

		for (AD1BomberCharacter* BC : Overlapping)
		{
			if (UD1BombPlacementComponent* Placement = BC->GetBombPlacement())
			{
				Placement->AddIgnoredBomb(this);
			}
		}
	}
}

void AD1Bomb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 소멸 경로 일원화: 미발화 도화선 타이머 정리.
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AD1Bomb::SetRange(int32 InRange)
{
	if (!HasAuthority())
	{
		return;
	}
	Range = FMath::Max(1, InRange);
}

void AD1Bomb::MulticastOnExploded_Implementation(const TArray<FIntPoint>& AffectedCells)
{
	// NetMulticast는 서버 로컬에서도 실행된다 — 렌더 없는 DS의 FX 스폰·Tick은 순수 낭비.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FIntPoint& Cell : AffectedCells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, UD1BomberGridLibrary::CellHalf);
		UClass* FXClass = ExplosionFXClass ? ExplosionFXClass.Get() : AD1ExplosionFX::StaticClass();
		World->SpawnActor<AD1ExplosionFX>(FXClass, Center, FRotator::ZeroRotator, Params);
	}
}

void AD1Bomb::DoExplode()
{
	if (!HasAuthority() || State == ED1BombState::Exploding)
	{
		return;
	}

	State = ED1BombState::Exploding;

	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	const FIntPoint Origin = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	TArray<FIntPoint> Cells;
	TArray<FIntPoint> SoftBlockHits;
	UD1BomberGridLibrary::TraceExplosionCells(GS, Origin, Range, Cells, SoftBlockHits);
	
	// 체인 격발 -> 소프트 블럭 파괴(이 Bomb의 폭발에 대해서만) -> 십자 위 파워업 파괴 -> 지속 피해 위험 영역 스폰(서버 전용) -> 폭발 이펙트 적용(클라 전용)
	{
		ChainDetonateBombs(Cells);
		DestroySoftBlocks(SoftBlockHits);
		DestroyPowerups(Cells);
		SpawnExplosionHazard(Cells);
		MulticastOnExploded(Cells);
	}

	// 소유자 폭탄 슬롯 회수 — Owner(설치 캐릭터)의 설치 컴포넌트에서 직접.
	if (AD1BomberCharacter* OwnerBC = GetOwner<AD1BomberCharacter>())
	{
		if (UD1BombPlacementComponent* Placement = OwnerBC->GetBombPlacement())
		{
			Placement->NotifyBombDestroyed(this);
		}
	}

	Destroy();
}

void AD1Bomb::DestroySoftBlocks(const TArray<FIntPoint>& SoftBlockHits)
{
	// 폭발 줄기가 닿은 파괴 가능 블록을 "파괴 중"으로 전환.
	// 셀 제거는 블록이 시간 경과 후 스스로 처리(파괴 중에도 폭발 차단 유지)
	UD1BomberGridLibrary::ForEachActorInCells<AD1SoftBlock>(GetWorld(), SoftBlockHits,
		[](AD1SoftBlock* Block)
		{
			if (!Block->IsDestroying())
			{
				Block->StartDestroying();
			}
		});
}

void AD1Bomb::DestroyPowerups(const TArray<FIntPoint>& Cells)
{
	// 폭발 십자에 걸린 드롭 아이템 파괴. Destroy()는 복제로 클라에서도 사라진다.
	UD1BomberGridLibrary::ForEachActorInCells<AD1PowerupPickup>(GetWorld(), Cells,
		[](AD1PowerupPickup* Pickup)
		{
			Pickup->Destroy();
		});
}

void AD1Bomb::SpawnExplosionHazard(const TArray<FIntPoint>& Cells)
{
	if (!HasAuthority() || Cells.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 서버 전용 위험 액터가 불꽃 수명 동안 피해를 담당(즉시 1차 + 지속 스윕).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* HazardClass = ExplosionHazardClass ? ExplosionHazardClass.Get() : AD1ExplosionHazard::StaticClass();
	if (AD1ExplosionHazard* Hazard = World->SpawnActor<AD1ExplosionHazard>(HazardClass, GetActorLocation(), FRotator::ZeroRotator, Params))
	{
		Hazard->SetupCells(Cells, ExplosionLingerDurationSec);
	}
}

void AD1Bomb::TriggerChainDetonation()
{
	if (!HasAuthority() || State != ED1BombState::Fusing)
	{
		return;
	}

	State = ED1BombState::Detonating;
	// SetTimer가 같은 핸들의 도화선 타이머를 자동으로 clear 후 교체한다.
	GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, ChainDetonationDelay, false);
}

void AD1Bomb::ChainDetonateBombs(const TArray<FIntPoint>& Cells)
{
	UD1BomberGridLibrary::ForEachActorInCells<AD1Bomb>(GetWorld(), Cells,
		[this](AD1Bomb* Other)
		{
			if (Other != this && Other->State == ED1BombState::Fusing)
			{
				Other->TriggerChainDetonation();
			}
		});
}
