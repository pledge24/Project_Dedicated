// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1WallBlock.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Game/D1BomberGridLibrary.h"

AD1WallBlock::AD1WallBlock()
{
	PrimaryActorTick.bCanEverTick = false;
	// 런타임에 서버가 데이터로부터 스폰 → 클라에 액터 존재·초기 위치 복제 필요.
	// 메시·콜리전은 생성자에서 세팅되므로 클라도 동일하게 구성됨(런타임 복제 프로퍼티 불필요).
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

	// /Engine/BasicShapes/Cube는 100cm, 피벗 중앙. 셀(100x100x100)에 딱 맞춤.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshAsset.Succeeded())
	{
		MeshComp->SetStaticMesh(CubeMeshAsset.Object);
	}
}
