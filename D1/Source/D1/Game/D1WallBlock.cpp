// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1WallBlock.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Game/D1BomberGridLibrary.h"

AD1WallBlock::AD1WallBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	// 런타임에 서버가 데이터로부터 스폰 → 클라에 액터 존재·초기 위치 복제 필요.
	// 메시는 BP 기본값, 콜리전은 생성자에서 결정돼 클라도 동일 구성(런타임 복제 프로퍼티 불필요).
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
	// 메시는 BP(BP_WallBlock)에서 지정.
}
