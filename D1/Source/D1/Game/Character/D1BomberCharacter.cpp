// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/Character/D1BomberCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

#include "Core/D1LogChannels.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Game/Character/D1BombPlacementComponent.h"
#include "Game/Character/D1BomberCharacterMovementComponent.h"
#include "Game/Character/D1BomberCosmeticComponent.h"

AD1BomberCharacter::AD1BomberCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UD1BomberCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	// 캐릭터 자체 Tick 없음 — 폭탄 통과 추적은 BombPlacementComp의 Tick.
	PrimaryActorTick.bCanEverTick = false;

	BombPlacementComp = CreateDefaultSubobject<UD1BombPlacementComponent>(TEXT("BombPlacementComp"));
	CosmeticComp = CreateDefaultSubobject<UD1BomberCosmeticComponent>(TEXT("CosmeticComp"));

	// 컨트롤러 회전 안 씀, 이동이 캐릭터 방향을 결정.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 500.f, 0.f);
		Move->MaxWalkSpeed = BaseWalkSpeed;
		Move->MinAnalogWalkSpeed = 20.f;
		Move->BrakingDecelerationWalking = 2000.f;
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(42.f, 96.f);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void AD1BomberCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberCharacter, bIsInvulnerable);
	DOREPLIFETIME(AD1BomberCharacter, bStunned);
}

void AD1BomberCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshPlayerStateBinding();			// PS -> Pawn 순으로 Replicate 된 경우.
	CosmeticComp->RefreshLocalHighlight();	// 리슨 호스트 본인 폰 강조(서버 전용 경로).
}

void AD1BomberCharacter::Restart()
{
	Super::Restart();

	// 시작 게이트: DoMove의 페이즈 게이트는 클라 입력 경로라 ServerMove가 재검증하지 않는다 —
	// 조작 클라의 시작 전 이동은 서버가 직접 잠가야 한다. StartMatch가 해제.
	// PossessedBy가 아닌 여기인 이유: 소유 흐름 마지막의 Super::Restart(SetDefaultMovementMode)가
	// 이동 모드를 Walking으로 되돌린다.
	if (!HasAuthority())
	{
		return;
	}

	const AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (GS && GS->GetMatchPhase() != EBomberMatchPhase::Playing)
	{
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->SetMovementMode(MOVE_None);
		}
	}
}

void AD1BomberCharacter::BeginPlay()
{
	Super::BeginPlay();

	// PossessedBy/OnRep_PlayerState가 BeginPlay 전에 와서 PS는 잡혔지만 컴포넌트(특히 WidgetComponent)가
	// 아직 init 안 됐을 수 있다. 여기서 한 번 더 ready 신호를 발화해 BP가 안전하게 위젯에 접근하게 함.
	if (PSWeakPtr.IsValid())
	{
		OnPlayerStateReady();
	}

	CosmeticComp->RefreshLocalHighlight();	// 컨트롤러가 이미 세팅된 경우(스탠드얼론/PIE) 안전망.
}

void AD1BomberCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AD1BomberCharacter::OnMoveInput);
		}
		if (PlaceBombAction && BombPlacementComp)
		{
			EIC->BindAction(PlaceBombAction, ETriggerEvent::Started,
				BombPlacementComp.Get(), &UD1BombPlacementComponent::ServerTryPlaceBomb);
		}
	}
}

void AD1BomberCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshPlayerStateBinding();	// Pawn -> PS 순으로 Replicate 된 경우.
}

void AD1BomberCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	CosmeticComp->RefreshLocalHighlight();	// 소유 클라에 컨트롤러 복제 도착 시 강조 갱신.
}

void AD1BomberCharacter::DoMove(float Right, float Forward)
{
	if (bStunned)
	{
		return;
	}

	// 죽은 폰 예측 이동 방지 — 이동 차단이 서버 권위로 옮겨져, 오너 클라의 입력을 여기서 막는다.
	const AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && !PS->IsAlive())
	{
		return;
	}

	// 카운트다운 시작(Playing) 전·종료 후엔 이동 불가 → "이동 가능 == 게임 시작" 일치(폭탄 게이트와 동일).
	const AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (GS && GS->GetMatchPhase() != EBomberMatchPhase::Playing)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->PlayerCameraManager)
	{
		const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
		const FRotator YawOnly(0.f, CamRot.Yaw, 0.f);
		const FVector Fwd = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
		const FVector Rgt = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
		AddMovementInput(Fwd, Forward);
		AddMovementInput(Rgt, Right);
		return;
	}
	if (GetController() != nullptr)
	{
		AddMovementInput(FVector::ForwardVector, Forward);
		AddMovementInput(FVector::RightVector, Right);
	}
}

void AD1BomberCharacter::OnMoveInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	DoMove(Axis.X, Axis.Y);
}

void AD1BomberCharacter::HandleSpeedLevelChanged()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (!PS)
	{
		return;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = BaseWalkSpeed + PS->GetSpeedLevel() * SpeedStep;
	}
}

void AD1BomberCharacter::ReceiveExplosionHit()
{
	if (!HasAuthority() || bIsInvulnerable)
	{
		return;
	}

	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (!PS || !PS->IsAlive())
	{
		return;
	}

	const bool bKilled = PS->ApplyHit();
	UE_LOG(LogD1, Log, TEXT("Explosion hit: %s Lives=%d killed=%d"),
		*GetName(), PS->GetLives(), bKilled ? 1 : 0);

	// 사망하지 않은 경우 피격 연출.(사망 이벤트는 PS의 bAlive OnRep 바인딩으로 처리)
	if (!bKilled)
	{
		StartInvulnerability(HitInvulnSec);
		ApplyHitStun();
	}
}

void AD1BomberCharacter::OnRep_bIsInvulnerable()
{
	// Start/EndInvulnerability의 수동 OnRep 호출로 서버(리슨 호스트)에서도 불린다 — DS 스킵은 컴포넌트가 담당.
	if (bIsInvulnerable)
	{
		CosmeticComp->PlayHitReaction(/*bWithAnim=*/!bDeathHandled);
	}
	else
	{
		CosmeticComp->StopHitReaction();
	}
}

void AD1BomberCharacter::StartInvulnerability(float DurationSec)
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInvulnerable = true;
	GetWorldTimerManager().SetTimer(InvulnTimerHandle, this,
		&AD1BomberCharacter::EndInvulnerability, DurationSec, false);
	OnRep_bIsInvulnerable();
}

void AD1BomberCharacter::EndInvulnerability()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInvulnerable = false;
	OnRep_bIsInvulnerable();
}

void AD1BomberCharacter::ApplyHitStun()
{
	if (!HasAuthority())
	{
		return;
	}
	bStunned = true;
	GetWorldTimerManager().SetTimer(StunTimerHandle, this,
		&AD1BomberCharacter::EndStun, StunDurationSec, false);
}

void AD1BomberCharacter::EndStun()
{
	bStunned = false;
}

void AD1BomberCharacter::HandleDeath()
{
	if (bDeathHandled)
	{
		return;
	}
	bDeathHandled = true;

	// 권위 정리(서버 전용): 콜리전/이동 차단 + 무적 지속 타이머 취소.
	// 이동은 ReplicatedMovementMode로 클라 자동 수렴, 콜리전은 클라에서 무관(ignore-Pawn/Visibility).
	if (HasAuthority())
	{
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->DisableMovement();
		}
		GetWorldTimerManager().ClearTimer(InvulnTimerHandle);
	}

	CosmeticComp->PlayDeathCosmetics();
}

void AD1BomberCharacter::HandlePlayerAliveStateChanged()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && !PS->IsAlive())
	{
		HandleDeath();
	}
}

void AD1BomberCharacter::HandleLeft()
{
	if (bLeftHandled)
	{
		return;
	}
	bLeftHandled = true;

	// 서버 권위 정리 — 콜리전/이동 차단(사망과 동일 teardown, 단 사망 파이프라인과는 별개 트리거).
	if (HasAuthority())
	{
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->DisableMovement();
		}
	}

	CosmeticComp->PlayLeftCosmetics();
}

void AD1BomberCharacter::HandlePlayerLeftChanged()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && PS->HasLeft())
	{
		HandleLeft();
	}
}

void AD1BomberCharacter::HandlePlayerNameRefreshed()
{
	// 이름이 늦게 들어오는 케이스(Listen Server 호스트 자기 PS 포함) 대응:
	// BP의 OnPlayerStateReady를 재호출해 이름표 SetText를 다시 트리거.
	if (HasActorBegunPlay())
	{
		OnPlayerStateReady();
	}
}

void AD1BomberCharacter::RefreshPlayerStateBinding()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (!PS || PSWeakPtr.Get() == PS)
	{
		return;
	}

	if (AD1BomberPlayerState* Prev = PSWeakPtr.Get())
	{
		Prev->OnAliveStateChanged.RemoveDynamic(this, &AD1BomberCharacter::HandlePlayerAliveStateChanged);
		Prev->OnPlayerNameChanged.RemoveDynamic(this, &AD1BomberCharacter::HandlePlayerNameRefreshed);
		Prev->OnSpeedLevelChanged.RemoveDynamic(this, &AD1BomberCharacter::HandleSpeedLevelChanged);
		Prev->OnLeftChanged.RemoveDynamic(this, &AD1BomberCharacter::HandlePlayerLeftChanged);
	}
	PS->OnAliveStateChanged.AddDynamic(this, &AD1BomberCharacter::HandlePlayerAliveStateChanged);
	PS->OnPlayerNameChanged.AddDynamic(this, &AD1BomberCharacter::HandlePlayerNameRefreshed);
	PS->OnSpeedLevelChanged.AddDynamic(this, &AD1BomberCharacter::HandleSpeedLevelChanged);
	PS->OnLeftChanged.AddDynamic(this, &AD1BomberCharacter::HandlePlayerLeftChanged);
	PSWeakPtr = PS;

	// 늦게 합류한 클라가 이미 올라간 SpeedLevel을 받았을 때 즉시 반영.
	HandleSpeedLevelChanged();

	// BP가 PS 확보 시점을 받게 함 (이름표 UI 등). BeginPlay 전에는 컴포넌트가 아직 init 안 됐을 수 있어
	// 신호를 미루고, BeginPlay에서 다시 한 번 발화한다.
	if (HasActorBegunPlay())
	{
		OnPlayerStateReady();
	}

	// 늦게 합류한 클라가 이미 사망 상태를 받았을 때 즉시 반영.
	if (!PS->IsAlive())
	{
		HandlePlayerAliveStateChanged();
	}

	// 늦게 합류한 클라가 이미 탈주 상태를 받았을 때 즉시 반영.
	if (PS->HasLeft())
	{
		HandlePlayerLeftChanged();
	}
}
