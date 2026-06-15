// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1SoftBlock.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/D1PowerupPickup.h"

AD1SoftBlock::AD1SoftBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionComp"));
	CollisionComp->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComp->SetCollisionObjectType(ECC_WorldStatic);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Block);
	RootComponent = CollisionComp;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// /Engine/BasicShapes/Cube는 100cm, 피벗 중앙. 셀(100x100x100)에 딱 맞춤.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshAsset.Succeeded())
	{
		MeshComp->SetStaticMesh(CubeMeshAsset.Object);
	}

	// 파괴 중 반투명 머티리얼 기본값. 에셋 없으면 null → 반투명 표현만 생략.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DyingMat(
		TEXT("/Game/D1/Materials/M_SoftBlock_Dying.M_SoftBlock_Dying"));
	if (DyingMat.Succeeded())
	{
		DyingMaterial = DyingMat.Object;
	}
}

void AD1SoftBlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1SoftBlock, bDying);
}

void AD1SoftBlock::StartDying()
{
	if (!HasAuthority() || bDying)
	{
		return;
	}

	bDying = true;

	// OnRep은 서버 자신에게 안 불리므로(리슨 서버) 수동 호출. DS는 보이는 화면이 없어 무해.
	OnRep_bDying();

	// 콜리전·셀은 그대로 둔 채 일정 시간 후 실제 파괴.
	GetWorldTimerManager().SetTimer(
		DyingTimerHandle, this, &AD1SoftBlock::CompleteDestruction, DyingDurationSec, /*bLoop=*/false);
}

void AD1SoftBlock::SetHeldItem(EPowerupType InType, TSubclassOf<AD1PowerupPickup> InPickupClass, float InDropZ)
{
	if (!HasAuthority())
	{
		return;
	}
	HeldItem = InType;
	PickupClass = InPickupClass;
	DropZ = InDropZ;
	bHasItem = true;
}

void AD1SoftBlock::OnRep_bDying()
{
	if (!bDying)
	{
		return;
	}

	// 반투명 머티리얼로 교체만. 콜리전은 건드리지 않음(파괴 중에도 통과 불가).
	if (DyingMaterial)
	{
		MeshComp->SetMaterial(0, DyingMaterial);
	}
}

void AD1SoftBlock::CompleteDestruction()
{
	UWorld* World = GetWorld();
	const FIntPoint Cell = UD1BomberGridLibrary::WorldToCell(GetActorLocation());

	// 이제서야 폭발 차단/통과 차단을 해제 — 셀 목록에서 빼고 액터 제거(복제로 클라 정리).
	if (AD1BomberGameState* GS = World ? World->GetGameState<AD1BomberGameState>() : nullptr)
	{
		GS->RemoveSoftBlockCell(Cell);
	}

	// 빌드 시 사전 배정된 아이템이 있으면 직접 스폰(서버 권위). 파괴 시점 굴림 없음.
	if (bHasItem && PickupClass && World)
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
