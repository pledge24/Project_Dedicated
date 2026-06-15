// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/Character/D1BomberCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

#include "Core/D1LogChannels.h"
#include "Game/D1Bomb.h"
#include "Game/Character/D1BomberCharacterMovementComponent.h"
#include "Framework/D1BomberGameState.h"
#include "Game/D1BomberGridLibrary.h"
#include "Framework/D1BomberPlayerState.h"

AD1BomberCharacter::AD1BomberCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UD1BomberCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	bIsInvulnerable = false;
	bBlinkVisible = true;

	// 탑다운: 컨트롤러 회전 안 씀, 이동이 캐릭터 방향을 결정.
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

	// 캐릭터끼리 충돌 안 함.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(42.f, 96.f);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void AD1BomberCharacter::BeginPlay()
{
	Super::BeginPlay();

	// PossessedBy/OnRep_PlayerState가 BeginPlay 전에 와서 PS는 잡혔지만 컴포넌트(특히 WidgetComponent)가
	// 아직 init 안 됐을 수 있다. 여기서 한 번 더 ready 신호를 발화해 BP가 안전하게 위젯에 접근하게 함.
	if (BoundPlayerState.IsValid())
	{
		OnPlayerStateReady();
	}
}

void AD1BomberCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 서버/클라 양쪽에서 실행해 양쪽 캡슐 스윕이 일치하도록.
	// (클라 이동 예측은 자체 MoveIgnoreActors 리스트를 따로 가짐.)
	UpdateIgnoredBombs();
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
		if (PlaceBombAction)
		{
			EIC->BindAction(PlaceBombAction, ETriggerEvent::Started, this, &AD1BomberCharacter::ServerTryPlaceBomb);
		}
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
	RefreshPlayerStateBinding();
}

void AD1BomberCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshPlayerStateBinding();
}

void AD1BomberCharacter::DoMove(float Right, float Forward)
{
	if (bStunned)
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

void AD1BomberCharacter::HandleDeath()
{
	if (bDeathHandled)
	{
		return;
	}
	bDeathHandled = true;

	// 충돌·이동 즉시 정지.
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->DisableMovement();
	}

	// 머리 위 이름표(Screen Space 위젯)는 SetVisibility로 꺼야 한다.
	TArray<UWidgetComponent*> WidgetComps;
	GetComponents<UWidgetComponent>(WidgetComps);
	for (UWidgetComponent* WC : WidgetComps)
	{
		WC->SetVisibility(false);
	}

	// 무적 타이머 중단 — 죽은 뒤 EndInvulnerability가 메시를 다시 켜는 사고 방지.
	GetWorldTimerManager().ClearTimer(InvulnTimerHandle);

	// 사망 연출: 피격처럼 깜빡이게 하고 사망 몽타주 재생.
	bBlinkVisible = true;
	if (USkeletalMeshComponent* SK = GetMesh())
	{
		SK->SetVisibility(true);
	}
	GetWorldTimerManager().SetTimer(BlinkTimerHandle, this,
		&AD1BomberCharacter::TickBlink, 0.1f, true);

	// 몽타주는 각 인스턴스에서 로컬 재생(HandleDeath가 서버·클라 양쪽에서 불림 → RPC 불필요).
	float HideAfter = DeathHideDelay;
	if (DeathMontage)
	{
		if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			const float MontageLen = AnimInst->Montage_Play(DeathMontage);
			if (MontageLen > 0.f)
			{
				HideAfter = MontageLen + DeathHideDelay;
			}
		}
	}

	// 사망 애니 종료 + 딜레이 후 메시 숨김.
	GetWorldTimerManager().SetTimer(DeathHideTimerHandle, this,
		&AD1BomberCharacter::FinishDeath, HideAfter, false);
}

void AD1BomberCharacter::StartInvulnerability(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInvulnerable = true;
	GetWorldTimerManager().SetTimer(InvulnTimerHandle, this,
		&AD1BomberCharacter::EndInvulnerability, Duration, false);
	OnRep_Invulnerable();
}

void AD1BomberCharacter::ApplyHitStun()
{
	if (!HasAuthority())
	{
		return;
	}
	bStunned = true;
	GetWorldTimerManager().SetTimer(StunTimerHandle, this,
		&AD1BomberCharacter::EndStun, StunDuration, false);
}

void AD1BomberCharacter::NotifyBombDestroyed(AD1Bomb* Bomb)
{
	// 폭탄이 터지면서 호출 — 소유자 슬롯 회수.
	ActiveBombs.RemoveAll([Bomb](const TWeakObjectPtr<AD1Bomb>& W)
	{
		return !W.IsValid() || W.Get() == Bomb;
	});

	if (Bomb)
	{
		IgnoredBombs.Remove(Bomb);
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			Cap->IgnoreActorWhenMoving(Bomb, false);
		}
	}
}

void AD1BomberCharacter::AddIgnoredBomb(AD1Bomb* Bomb)
{
	/** 서버 전용: 지금 겹치고 있는 폭탄을 등록.
	 *  같은 셀에 있는 동안 캡슐이 폭탄을 통과시키고,
	 *  셀을 벗어나면 Tick 정리에서 차단 복원해 재진입 막음. */

	if (!Bomb)
	{
		return;
	}
	IgnoredBombs.Add(Bomb);
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->IgnoreActorWhenMoving(Bomb, true);
	}
}

void AD1BomberCharacter::ServerTryPlaceBomb_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	if (bStunned)
	{
		return;
	}

	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	const int32 BombCap = PS ? PS->BombCapacity : 1;
	if (GetActiveBombCount() >= BombCap)
	{
		return;
	}
	if (!BombClass)
	{
		UE_LOG(LogD1, Warning, TEXT("BomberCharacter: BombClass not set"));
		return;
	}

	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (GS && GS->MatchPhase != EBomberMatchPhase::Playing)
	{
		return;
	}

	if (PS && !PS->bIsAlive)
	{
		return;
	}

	const FIntPoint Cell = UD1BomberGridLibrary::WorldToCell(GetActorLocation());
	if (GS && !GS->IsInsideGrid(Cell))
	{
		return;
	}
	if (GS && GS->IsWallCell(Cell))
	{
		return;
	}

	// 한 셀에 폭탄 하나 룰 — 자기 것뿐 아니라 월드 전역의 모든 폭탄을 검사.
	// 캐릭터-캐릭터 충돌이 꺼져 있어 두 명이 같은 셀에 설 수 있고 둘 다
	// 폭탄 IgnoredBombs에 들어갈 수 있어서, 자기 슬롯만 보면 두 폭탄이 겹친다.
	for (TActorIterator<AD1Bomb> It(GetWorld()); It; ++It)
	{
		const AD1Bomb* Existing = *It;
		if (!IsValid(Existing))
		{
			continue;
		}
		if (UD1BomberGridLibrary::WorldToCell(Existing->GetActorLocation()) == Cell)
		{
			return;
		}
	}

	// ============ 검증 종료 =============

	const FVector SpawnLoc = UD1BomberGridLibrary::CellToWorldCenter(Cell, 50.f);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AD1Bomb* Bomb = GetWorld()->SpawnActor<AD1Bomb>(BombClass, SpawnLoc, FRotator::ZeroRotator, Params);
	if (!Bomb)
	{
		return;
	}

	if (PS)
	{
		Bomb->SetRange(PS->FirePower); // 설치자 화력 stamp
	}
	ActiveBombs.Add(Bomb);
	// 폭탄이 BeginPlay에서 겹친 캐릭터(소유자 포함)를 모두 IgnoredBombs에 등록함.
	// 여기선 슬롯만 추적.
}

void AD1BomberCharacter::OnRep_Invulnerable()
{
	if (bIsInvulnerable)
	{
		// 피격 리액션: AS_HitBomb를 DefaultSlot에 동적 몽타주로 재생.
		// invuln 복제로 모든 인스턴스에서 OnRep_Invulnerable이 불려 함께 재생됨.
		if (HitAnim && !bDeathHandled)
		{
			if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				AnimInst->PlaySlotAnimationAsDynamicMontage(HitAnim, TEXT("DefaultSlot"));
			}
		}

		bBlinkVisible = true;
		GetWorldTimerManager().SetTimer(BlinkTimerHandle, this,
			&AD1BomberCharacter::TickBlink, 0.1f, true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(BlinkTimerHandle);
		if (USkeletalMeshComponent* SK = GetMesh())
		{
			SK->SetVisibility(true);
		}
	}
}

void AD1BomberCharacter::OnPlayerAliveStateChanged()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && !PS->bIsAlive)
	{
		HandleDeath();
	}
}

void AD1BomberCharacter::OnPlayerNameRefreshed()
{
	// 이름이 늦게 들어오는 케이스(Listen Server 호스트 자기 PS 포함) 대응:
	// BP의 OnPlayerStateReady를 재호출해 이름표 SetText를 다시 트리거.
	if (HasActorBegunPlay())
	{
		OnPlayerStateReady();
	}
}

void AD1BomberCharacter::OnSpeedLevelChanged()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (!PS)
	{
		return;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = BaseWalkSpeed + PS->SpeedLevel * SpeedStep;
	}
}

void AD1BomberCharacter::OnMoveInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	DoMove(Axis.X, Axis.Y);
}

int32 AD1BomberCharacter::GetActiveBombCount()
{
	for (int32 i = ActiveBombs.Num() - 1; i >= 0; --i)
	{
		if (!ActiveBombs[i].IsValid())
		{
			ActiveBombs.RemoveAtSwap(i);
		}
	}
	return ActiveBombs.Num();
}

void AD1BomberCharacter::EndInvulnerability()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInvulnerable = false;
	OnRep_Invulnerable();
}

void AD1BomberCharacter::TickBlink()
{
	bBlinkVisible = !bBlinkVisible;
	if (USkeletalMeshComponent* SK = GetMesh())
	{
		SK->SetVisibility(bBlinkVisible);
	}
}

void AD1BomberCharacter::UpdateIgnoredBombs()
{
	if (IgnoredBombs.Num() == 0)
	{
		return;
	}

	UCapsuleComponent* Cap = GetCapsuleComponent();
	if (!Cap)
	{
		return;
	}

	// "폭탄 영역 벗어남" 판정: 캡슐이 폭탄 박스 콜리전 밖으로 완전히 나간 뒤에야
	// 차단을 다시 켠다. 캡슐 반지름 + 박스 반폭 + 여유 거리로 계산해서,
	// IgnoreActorWhenMoving을 false로 되돌릴 때 캡슐이 박스에 끼어 튕겨나가는 거 방지.
	const float CapRadius = Cap->GetScaledCapsuleRadius();
	const float BombHalfExtent = 50.f; // bomb cell footprint, see AD1Bomb
	const float ExitMargin = 5.f;
	const float ExitDistanceSquared = FMath::Square(CapRadius + BombHalfExtent + ExitMargin);

	const FVector MyLoc = GetActorLocation();

	TArray<TWeakObjectPtr<AD1Bomb>> ToRemove;
	for (const TWeakObjectPtr<AD1Bomb>& WB : IgnoredBombs)
	{
		AD1Bomb* Bomb = WB.Get();
		if (!Bomb)
		{
			ToRemove.Add(WB);
			continue;
		}

		const FVector BombLoc = Bomb->GetActorLocation();
		const FVector Delta(MyLoc.X - BombLoc.X, MyLoc.Y - BombLoc.Y, 0.f);
		if (Delta.SizeSquared() > ExitDistanceSquared)
		{
			Cap->IgnoreActorWhenMoving(Bomb, false);
			ToRemove.Add(WB);
		}
	}

	for (const TWeakObjectPtr<AD1Bomb>& W : ToRemove)
	{
		IgnoredBombs.Remove(W);
	}
}

void AD1BomberCharacter::RefreshPlayerStateBinding()
{
	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (!PS || BoundPlayerState.Get() == PS)
	{
		return;
	}

	if (AD1BomberPlayerState* Prev = BoundPlayerState.Get())
	{
		Prev->OnAliveStateChanged.RemoveDynamic(this, &AD1BomberCharacter::OnPlayerAliveStateChanged);
		Prev->OnPlayerNameChanged.RemoveDynamic(this, &AD1BomberCharacter::OnPlayerNameRefreshed);
		Prev->OnSpeedLevelChanged.RemoveDynamic(this, &AD1BomberCharacter::OnSpeedLevelChanged);
	}
	PS->OnAliveStateChanged.AddDynamic(this, &AD1BomberCharacter::OnPlayerAliveStateChanged);
	PS->OnPlayerNameChanged.AddDynamic(this, &AD1BomberCharacter::OnPlayerNameRefreshed);
	PS->OnSpeedLevelChanged.AddDynamic(this, &AD1BomberCharacter::OnSpeedLevelChanged);
	BoundPlayerState = PS;

	// 늦게 합류한 클라가 이미 올라간 SpeedLevel을 받았을 때 즉시 반영.
	OnSpeedLevelChanged();

	// BP가 PS 확보 시점을 받게 함 (이름표 UI 등). BeginPlay 전에는 컴포넌트가 아직 init 안 됐을 수 있어
	// 신호를 미루고, BeginPlay에서 다시 한 번 발화한다.
	if (HasActorBegunPlay())
	{
		OnPlayerStateReady();
	}

	// 늦게 합류한 클라가 이미 사망 상태를 받았을 때 즉시 반영.
	if (!PS->bIsAlive)
	{
		OnPlayerAliveStateChanged();
	}
}

void AD1BomberCharacter::FinishDeath()
{
	GetWorldTimerManager().ClearTimer(BlinkTimerHandle);
	if (USkeletalMeshComponent* SK = GetMesh())
	{
		SK->SetVisibility(false);
	}
}

void AD1BomberCharacter::EndStun()
{
	bStunned = false;
}
