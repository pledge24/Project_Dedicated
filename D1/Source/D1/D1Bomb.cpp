// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1Bomb.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/KismetSystemLibrary.h"

#include "D1.h"
#include "D1BomberCharacter.h"
#include "D1BomberGameMode.h"
#include "D1BomberGameState.h"
#include "D1BomberGridLibrary.h"
#include "D1BomberPlayerState.h"

AD1Bomb::AD1Bomb()
{
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	PrimaryActorTick.bCanEverTick = false;

	Range = 2;
	FuseSeconds = 3.f;
	DetonationServerTime = 0.f;

	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
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
	DOREPLIFETIME(AD1Bomb, OwningPlayerState);
	DOREPLIFETIME(AD1Bomb, DetonationServerTime);
}

void AD1Bomb::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		DetonationServerTime = GetWorld()->GetTimeSeconds() + FuseSeconds;
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AD1Bomb::DoExplode, FuseSeconds, false);
	}
}

void AD1Bomb::Initialize(AD1BomberPlayerState* InOwner)
{
	OwningPlayerState = InOwner;
}

void AD1Bomb::DoExplode()
{
	if (!HasAuthority())
	{
		return;
	}

	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	const FIntPoint Origin = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	TArray<FIntPoint> Cells;
	UD1BomberGridLibrary::EnumerateCrossCells(GS, Origin, Range, Cells);
	Cells.Insert(Origin, 0);

	AD1BomberGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AD1BomberGameMode>() : nullptr;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TSet<AD1BomberCharacter*> AlreadyHit;
	for (const FIntPoint& Cell : Cells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, 50.f);
		TArray<AActor*> Found;
		UKismetSystemLibrary::BoxOverlapActors(this, Center,
			FVector(45.f, 45.f, 80.f),
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
				BC->HandleDeath();
				if (GM)
				{
					GM->NotifyPlayerDied(PS);
				}
			}
			else
			{
				BC->StartInvulnerability(2.0f);
			}
		}
	}

	MulticastOnExploded(Cells);

	// Free the owner's bomb slot.
	if (OwningPlayerState)
	{
		if (APawn* Pawn = OwningPlayerState->GetPawn())
		{
			if (AD1BomberCharacter* OwnerBC = Cast<AD1BomberCharacter>(Pawn))
			{
				OwnerBC->NotifyBombDestroyed(this);
			}
		}
	}

	Destroy();
}

void AD1Bomb::MulticastOnExploded_Implementation(const TArray<FIntPoint>& AffectedCells)
{
	for (const FIntPoint& Cell : AffectedCells)
	{
		const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(Cell, 50.f);
		DrawDebugBox(GetWorld(), Center, FVector(45.f, 45.f, 45.f), FColor::Orange, false, 0.6f);
	}
}

void AD1Bomb::OnRep_DetonationServerTime()
{
	// Placeholder for client-side countdown VFX (mesh pulse, sound, etc.).
}
