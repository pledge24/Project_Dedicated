// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1ExplosionFX.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AD1ExplosionFX::AD1ExplosionFX()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Lifetime = 0.5f;
	ExpansionTime = 0.2f;
	PeakScale = 0.9f;
	Elapsed = 0.f;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComp->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ExplosionMat(
		TEXT("/Game/D1/Materials/M_ExplosionFX.M_ExplosionFX"));
	if (ExplosionMat.Succeeded())
	{
		MeshComp->SetMaterial(0, ExplosionMat.Object);
	}

	SetActorScale3D(FVector(0.05f));
}

void AD1ExplosionFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;
	if (Elapsed >= Lifetime)
	{
		Destroy();
		return;
	}

	float Scale;
	if (Elapsed < ExpansionTime)
	{
		const float A = FMath::Clamp(Elapsed / FMath::Max(ExpansionTime, KINDA_SMALL_NUMBER), 0.f, 1.f);
		Scale = FMath::Lerp(0.05f, PeakScale, A);
	}
	else
	{
		const float HoldDuration = FMath::Max(Lifetime - ExpansionTime, KINDA_SMALL_NUMBER);
		const float A = FMath::Clamp((Elapsed - ExpansionTime) / HoldDuration, 0.f, 1.f);
		Scale = FMath::Lerp(PeakScale, 0.05f, A);
	}
	SetActorScale3D(FVector(Scale));
}

void AD1ExplosionFX::BeginPlay()
{
	Super::BeginPlay();
	Elapsed = 0.f;
}
