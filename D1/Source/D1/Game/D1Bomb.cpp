// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1Bomb.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

#include "Game/Character/D1BomberCharacter.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/D1ExplosionFX.h"
#include "Game/D1ExplosionHazard.h"
#include "Game/D1SoftBlock.h"

AD1Bomb::AD1Bomb()
{
	bReplicates = true;
	
	// 최적화 설정. (이동 rep 비활성화, 네트워크 갱신 주기 하향(100 -> 10), Tick 비활성화)
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	PrimaryActorTick.bCanEverTick = false;

	// 옆 칸에 있는 캐릭터가 폭탄 모서리에 끼는걸 방지하기 위해 충돌체 크기 100 -> 80으로 조정.
	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
	CollisionComp->InitBoxExtent(FVector(40.f, 40.f, 40.f));
	CollisionComp->SetCollisionProfileName(TEXT("BomberBomb"));
	RootComponent = CollisionComp;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AD1Bomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1Bomb, DetonationServerTime);
}

void AD1Bomb::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		DetonationServerTime = GetWorld()->GetTimeSeconds() + FuseSec;
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, FuseSec, false);
	}

	// 폭탄이 스폰된 타이밍에 해당 셀 내부에 위치한 캐릭터들은 Sweep 충돌을 무시하도록 등록. (서버/클라 양쪽에서 실행)
	{
		TArray<AD1BomberCharacter*> Overlapping;
		AD1BomberCharacter::OverlapBomberCharacters(this, GetActorLocation(),
			FVector(UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellSize),
			Overlapping);

		for (AD1BomberCharacter* BC : Overlapping)
		{
			BC->AddIgnoredBomb(this);
		}
	}
}

void AD1Bomb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 소멸 경로 일원화: 미발화 도화선 타이머 정리.
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AD1Bomb::OnRep_DetonationServerTime()
{
	// 클라 카운트다운 VFX(메시 펄스, 사운드 등) 자리.
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
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FIntPoint& Cell : AffectedCells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, 50.f);
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

	// 이중 폭발 방지
	State = ED1BombState::Exploding;

	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	const FIntPoint Origin = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	TArray<FIntPoint> Cells;
	TArray<FIntPoint> SoftBlockHits;
	UD1BomberGridLibrary::TraceExplosionCells(GS, Origin, Range, Cells, SoftBlockHits);
	
	// 체인 격발 -> 소프트 블럭 파괴(이 Bomb의 폭발에 대해서만) -> 지속 피해 위험 영역 스폰(서버 전용) -> 폭발 이펙트 적용(클라 전용)
	{
		ChainDetonateBombs(Cells);
		DestroySoftBlocks(SoftBlockHits);
		SpawnExplosionHazard(Cells);
		MulticastOnExploded(Cells);
	}

	// 소유자 폭탄 슬롯 회수 — Owner(설치 캐릭터)에서 직접.
	if (AD1BomberCharacter* OwnerBC = GetOwner<AD1BomberCharacter>())
	{
		OwnerBC->NotifyBombDestroyed(this);
	}

	Destroy();
}

void AD1Bomb::DestroySoftBlocks(const TArray<FIntPoint>& SoftBlockHits)
{
	// 폭발 줄기가 닿은 파괴 가능 블록을 "파괴 중"으로 전환. 
	// 셀 제거는 블록이 시간 경과 후 스스로 처리(파괴 중에도 폭발 차단 유지)
	if (SoftBlockHits.Num() == 0)
	{
		return;
	}

	for (AD1SoftBlock* Block : TActorRange<AD1SoftBlock>(GetWorld()))
	{
		if (!IsValid(Block) || Block->IsDestroying())
		{
			continue;
		}

		const FIntPoint BlockCell = UD1BomberGridLibrary::WorldToCell(Block->GetActorLocation());
		if (SoftBlockHits.Contains(BlockCell))
		{
			Block->StartDestroying();
		}
	}
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
		Hazard->Initialize(Cells, ExplosionLingerDurationSec);
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
	// 폭발 십자 위에 있는 다른 폭탄 격발.
	for (AD1Bomb* Other : TActorRange<AD1Bomb>(GetWorld()))
	{
		if (!IsValid(Other) || Other == this)
		{
			continue;
		}
		
		if (Other->State != ED1BombState::Fusing)
		{
			continue;
		}

		const FIntPoint OtherCell = UD1BomberGridLibrary::WorldToCell(Other->GetActorLocation());
		if (Cells.Contains(OtherCell))
		{
			Other->TriggerChainDetonation();
		}
	}
}
