// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1Bomb.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/KismetSystemLibrary.h"

#include "Core/D1LogChannels.h"
#include "Game/Character/D1BomberCharacter.h"
#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Framework/D1BomberPlayerState.h"
#include "Game/D1ExplosionFX.h"
#include "Game/D1SoftBlock.h"

AD1Bomb::AD1Bomb()
{
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	PrimaryActorTick.bCanEverTick = false;

	Range = 2;
	FuseSec = 3.f;
	DetonationServerTime = 0.f;

	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
	// 셀(100)보다 작은 충돌 — 두 캐릭터가 폭탄과 같은 칸에 설 수 있게.
	CollisionComp->InitBoxExtent(FVector(40.f, 40.f, 40.f));
	CollisionComp->SetCollisionProfileName(TEXT("BomberBomb"));
	RootComponent = CollisionComp;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComp->SetStaticMesh(SphereMesh.Object);
		MeshComp->SetRelativeScale3D(FVector(0.7f));
	}
}

void AD1Bomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1Bomb, DetonationServerTime);
}

void AD1Bomb::SetRange(int32 InRange)
{
	if (!HasAuthority())
	{
		return;
	}
	Range = FMath::Max(1, InRange);
}

void AD1Bomb::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		DetonationServerTime = GetWorld()->GetTimeSeconds() + FuseSec;
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, FuseSec, false);
	}

	// 서버/클라 양쪽에서 실행. 양쪽 캡슐 스윕(클라 이동 예측 포함)이
	// 폭탄 셀에 들어와 있는 캐릭터는 셀을 벗어나기 전까지 통과로 처리되도록.
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> Overlapping;
	UKismetSystemLibrary::BoxOverlapActors(this, GetActorLocation(),
		FVector(UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellHalf, UD1BomberGridLibrary::CellSize),
		ObjectTypes,
		AD1BomberCharacter::StaticClass(),
		TArray<AActor*>(),
		Overlapping);

	for (AActor* A : Overlapping)
	{
		if (AD1BomberCharacter* BC = Cast<AD1BomberCharacter>(A))
		{
			BC->AddIgnoredBomb(this);
		}
	}
}

void AD1Bomb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 소멸 경로 일원화: 미발화 도화선 타이머 정리(엔진 자동 취소의 명시적 보강).
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
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
		World->SpawnActor<AD1ExplosionFX>(AD1ExplosionFX::StaticClass(), Center, FRotator::ZeroRotator, Params);
	}
}

void AD1Bomb::OnRep_DetonationServerTime()
{
	// 클라 카운트다운 VFX(메시 펄스, 사운드 등) 자리.
}

void AD1Bomb::DoExplode()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bIsExploding)
	{
		return;
	}
	bIsExploding = true;

	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	const FIntPoint Origin = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	TArray<FIntPoint> Cells;
	TArray<FIntPoint> SoftBlockHits;
	UD1BomberGridLibrary::TraceExplosionCells(GS, Origin, Range, Cells, SoftBlockHits);
	
	ChainDetonateBombs(Cells);
	DestroySoftBlocks(SoftBlockHits);
	ApplyExplosionDamage(Cells);

	MulticastOnExploded(Cells);

	// 소유자 폭탄 슬롯 회수 — Owner(설치 캐릭터)에서 직접.
	if (AD1BomberCharacter* OwnerBC = GetOwner<AD1BomberCharacter>())
	{
		OwnerBC->NotifyBombDestroyed(this);
	}

	Destroy();
}

void AD1Bomb::TriggerChainDetonation()
{
	if (!HasAuthority() || bIsExploding || bChainScheduled)
	{
		return;
	}

	bChainScheduled = true;
	// SetTimer가 같은 핸들의 도화선 타이머를 자동으로 clear 후 교체한다.
	GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, ChainDetonationDelay, false);
}

void AD1Bomb::ChainDetonateBombs(const TArray<FIntPoint>& Cells)
{
	// 폭발 십자에 걸린 다른 폭탄 체인 점화. (블록 셀은 Cells에 없으므로 블록 뒤 폭탄은 보호됨)
	const TSet<FIntPoint> CellSet(Cells);
	for (TActorIterator<AD1Bomb> It(GetWorld()); It; ++It)
	{
		AD1Bomb* Other = *It;
		if (!IsValid(Other) || Other == this)
		{
			continue;
		}
		if (Other->bIsExploding || Other->bChainScheduled)
		{
			continue;
		}

		const FIntPoint OtherCell = UD1BomberGridLibrary::WorldToCell(Other->GetActorLocation());
		if (CellSet.Contains(OtherCell))
		{
			Other->TriggerChainDetonation();
		}
	}
}

void AD1Bomb::DestroySoftBlocks(const TArray<FIntPoint>& SoftBlockHits)
{
	// 폭발 줄기가 닿은 파괴 가능 블록을 "파괴 중"으로 전환. 셀 제거는 블록이 시간 경과 후
	// 스스로 처리(파괴 중에도 폭발 차단 유지) — 여기선 StartDestroying만.
	if (SoftBlockHits.Num() == 0)
	{
		return;
	}

	const TSet<FIntPoint> HitSet(SoftBlockHits);
	for (TActorIterator<AD1SoftBlock> It(GetWorld()); It; ++It)
	{
		AD1SoftBlock* Block = *It;
		if (!IsValid(Block) || Block->IsDestroying())
		{
			continue;
		}

		const FIntPoint BlockCell = UD1BomberGridLibrary::WorldToCell(Block->GetActorLocation());
		if (HitSet.Contains(BlockCell))
		{
			Block->StartDestroying();
		}
	}
}

void AD1Bomb::ApplyExplosionDamage(const TArray<FIntPoint>& Cells)
{
	AD1BomberGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AD1BomberGameMode>() : nullptr;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	// 폭발 피격 박스: 셀보다 약간 작은 가로(45) + 캐릭터 높이(80).
	const FVector HitExtent(45.f, 45.f, 80.f);

	TSet<AD1BomberCharacter*> AlreadyHit;
	for (const FIntPoint& Cell : Cells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, UD1BomberGridLibrary::CellHalf);
		TArray<AActor*> Found;
		UKismetSystemLibrary::BoxOverlapActors(this, Center,
			HitExtent,
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

			if (BC->IsInvulnerable())
			{
				UE_LOG(LogD1, Log, TEXT("Bomb hit (invul ignored): %s"), *BC->GetName());
				continue;
			}

			AD1BomberPlayerState* PS = BC->GetPlayerState<AD1BomberPlayerState>();
			if (!PS || !PS->bIsAlive)
			{
				continue;
			}

			const bool bKilled = PS->ApplyHit();
			UE_LOG(LogD1, Log, TEXT("Bomb hit: %s Lives=%d killed=%d"),
				*BC->GetName(), PS->Lives, bKilled ? 1 : 0);

			if (bKilled)
			{
				// 캐릭터 사망 정리는 PS의 OnAliveStateChanged 바인딩이 처리.
				// (서버 측은 ApplyHit가 OnRep_bIsAlive를 수동 호출해 델리게이트가 즉시 발화.)
				if (GM)
				{
					GM->NotifyPlayerDied(PS);
				}
			}
			else
			{
				BC->StartInvulnerability(BC->GetHitInvulnSec());
				BC->ApplyHitStun();
			}
		}
	}
}
