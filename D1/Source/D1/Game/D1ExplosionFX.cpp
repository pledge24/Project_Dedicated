// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1ExplosionFX.h"
#include "Components/StaticMeshComponent.h"

AD1ExplosionFX::AD1ExplosionFX()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetCastShadow(false);
	// 메시·머티리얼은 BP(BP_ExplosionFX)에서 지정.

	SetActorScale3D(FVector(InitialScale));
}

void AD1ExplosionFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedSec += DeltaSeconds;
	if (ElapsedSec >= LifetimeSec)
	{
		Destroy();
		return;
	}

	float Scale;
	if (ElapsedSec < ExpansionTimeSec)
	{
		const float A = FMath::Clamp(ElapsedSec / FMath::Max(ExpansionTimeSec, KINDA_SMALL_NUMBER), 0.f, 1.f);
		Scale = FMath::Lerp(InitialScale, PeakScale, A);
	}
	else
	{
		const float HoldDuration = FMath::Max(LifetimeSec - ExpansionTimeSec, KINDA_SMALL_NUMBER);
		const float A = FMath::Clamp((ElapsedSec - ExpansionTimeSec) / HoldDuration, 0.f, 1.f);
		Scale = FMath::Lerp(PeakScale, InitialScale, A);
	}
	SetActorScale3D(FVector(Scale));
}

void AD1ExplosionFX::BeginPlay()
{
	Super::BeginPlay();
	ElapsedSec = 0.f;
}
