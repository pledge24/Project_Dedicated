// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/D1PowerupPickup.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/SphereComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

#include "Game/Character/D1BomberCharacter.h"
#include "Framework/D1BomberPlayerState.h"

AD1PowerupPickup::AD1PowerupPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
	CollisionComp->InitSphereRadius(45.f);
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComp->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComp->SetGenerateOverlapEvents(true);
	RootComponent = CollisionComp;

	BillboardComp = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("BillboardComp"));
	BillboardComp->SetupAttachment(RootComponent);
	BillboardComp->SetRelativeLocation(FVector(0.f, 0.f, BillboardBaseZ));
}

void AD1PowerupPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1PowerupPickup, PowerupType);
}

void AD1PowerupPickup::BeginPlay()
{
	Super::BeginPlay();

	// 데디 서버는 빌보드를 렌더하지 않음 — bob(코스메틱) tick은 헤드리스 서버에서 불필요.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
	}

	// 같은 프레임에 떨어진 여러 아이템이 똑같이 흔들리지 않게 위상 분산.
	const FVector Loc = GetActorLocation();
	BobPhase = FMath::Fmod(FMath::Abs(Loc.X + Loc.Y) * 0.01f, 2.f * PI);

	if (HasAuthority())
	{
		CollisionComp->OnComponentBeginOverlap.AddDynamic(this, &AD1PowerupPickup::OnSphereBeginOverlap);
	}

	RefreshVisual();
}

void AD1PowerupPickup::SetPowerupType(EPowerupType InType)
{
	if (!HasAuthority())
	{
		return;
	}
	PowerupType = InType;
	RefreshVisual(); // Listen Server 자기 화면 대응(DS는 무해)
}

void AD1PowerupPickup::OnRep_PowerupType()
{
	RefreshVisual();
}

void AD1PowerupPickup::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!HasAuthority())
	{
		return;
	}

	AD1BomberCharacter* BC = Cast<AD1BomberCharacter>(OtherActor);
	if (!BC)
	{
		return;
	}

	AD1BomberPlayerState* PS = BC->GetPlayerState<AD1BomberPlayerState>();
	if (!PS || !PS->IsAlive())
	{
		return;
	}

	switch (PowerupType)
	{
	case EPowerupType::Fire:
		PS->AddFirePower(1);
		break;
	case EPowerupType::Bomb:
		PS->AddBombCapacity(1);
		break;
	case EPowerupType::Speed:
		PS->AddSpeedLevel(1); // OnRep_SpeedLevel → 캐릭터 MaxWalkSpeed 반영
		break;
	}
	Destroy(); // 복제로 클라에서도 사라짐
}

void AD1PowerupPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!BillboardComp)
	{
		return;
	}
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Z = BillboardBaseZ + BobAmplitude * FMath::Sin(BobSpeed * T + BobPhase);
	BillboardComp->SetRelativeLocation(FVector(0.f, 0.f, Z));
}

void AD1PowerupPickup::RefreshVisual()
{
	if (!BillboardComp)
	{
		return;
	}

	UMaterialInterface* Mat = nullptr;
	const int32 Idx = static_cast<int32>(PowerupType);
	if (IconMaterials.IsValidIndex(Idx))
	{
		Mat = IconMaterials[Idx];
	}

	BillboardComp->Elements.Reset();
	BillboardComp->AddElement(Mat, nullptr, /*bSizeIsInScreenSpace=*/false, BillboardSize, BillboardSize, nullptr);
}
