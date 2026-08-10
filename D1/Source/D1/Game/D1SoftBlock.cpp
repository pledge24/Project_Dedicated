// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1SoftBlock.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/D1PowerupPickup.h"

AD1SoftBlock::AD1SoftBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
	CollisionComp->SetBoxExtent(FVector(UD1BomberGridLibrary::CellHalf));
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComp->SetCollisionObjectType(ECC_WorldStatic);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Block);
	RootComponent = CollisionComp;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 메시·파괴중 머티리얼은 BP(BP_SoftBlock)에서 지정.
}

void AD1SoftBlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1SoftBlock, bDestroying);
}

void AD1SoftBlock::StartDestroying()
{
	if (bDestroying)
	{
		return;
	}

	bDestroying = true;

	// OnRep은 서버 자신에게 안 불리므로(리슨 서버) 수동 호출. DS는 보이는 화면이 없어 무해.
	OnRep_bDestroying();

	// 콜리전·셀은 그대로 둔 채 일정 시간 후 실제 파괴.
	GetWorldTimerManager().SetTimer(
		DestroyingTimerHandle, this, &AD1SoftBlock::CompleteDestruction, DestroyingDurationSec, /*bLoop=*/false);
}

void AD1SoftBlock::OnRep_bDestroying()
{
	// 반투명 머티리얼로 교체만. 콜리전은 건드리지 않음(파괴 중에도 통과 불가).
	if (DestroyingMaterial)
	{
		MeshComp->SetMaterial(0, DestroyingMaterial);
	}
}

void AD1SoftBlock::CompleteDestruction()
{
	UWorld* World = GetWorld();
	const FIntPoint Cell = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	// 이제서야 폭발 차단/통과 차단을 해제 — 셀 목록에서 빼고 액터 제거(복제로 클라 정리).
	World->GetGameState<AD1BomberGameState>()->RemoveSoftBlockCell(Cell);

	// 빌드 시 사전 배정된 아이템이 있으면 직접 스폰(서버 권위, SetHeldItem이 클래스 유효를 보증).
	if (bHasItem)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Loc = UD1BomberGridLibrary::CellToWorldCenter(Cell, DropZ);
		if (AD1PowerupPickup* Pickup = World->SpawnActor<AD1PowerupPickup>(PickupClass, Loc, FRotator::ZeroRotator, Params))
		{
			Pickup->SetPowerupType(HeldItem);
		}
	}

	Destroy();
}

void AD1SoftBlock::SetHeldItem(EPowerupType InType, TSubclassOf<AD1PowerupPickup> InPickupClass, float InDropZ)
{
	if (!HasAuthority())
	{
		return;
	}
	// null 클래스로 bHasItem을 세우면 드롭이 스폰 지점에서 조용히 소실된다 — 배정 자체를 거부.
	if (!InPickupClass)
	{
		return;
	}
	HeldItem = InType;
	PickupClass = InPickupClass;
	DropZ = InDropZ;
	bHasItem = true;
}
