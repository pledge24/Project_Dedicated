// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1Bomb.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
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

	// 폭탄이 스폰된 타이밍에 해당 셀 내부에 위치한 캐릭터들은 Sweep 충돌을 무시하도록 등록. (서버/클라 양쪽에서 실행)
	{
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
}

void AD1Bomb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 소멸 경로 일원화: 미발화 도화선 타이머 정리.
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
		UClass* FXClass = ExplosionFXClass ? ExplosionFXClass.Get() : AD1ExplosionFX::StaticClass();
		World->SpawnActor<AD1ExplosionFX>(FXClass, Center, FRotator::ZeroRotator, Params);
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
	
	// 체인 격발 -> 소프트 블럭 파괴(이 Bomb의 폭발에 대해서만) -> 캐릭터에게 폭발 피해 적용 -> 폭발 이펙트 적용
	{
		ChainDetonateBombs(Cells);
		DestroySoftBlocks(SoftBlockHits);
		ApplyExplosionDamage(Cells);
		MulticastOnExploded(Cells);
	}

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
	// 폭발 십자에 걸린 다른 폭탄 체인 점화.
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
