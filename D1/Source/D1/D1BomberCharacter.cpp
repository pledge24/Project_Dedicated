// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

#include "D1.h"
#include "D1Bomb.h"
#include "D1BomberGameState.h"
#include "D1BomberGridLibrary.h"
#include "D1BomberPlayerState.h"

AD1BomberCharacter::AD1BomberCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bIsInvulnerable = false;
	bBlinkVisible = true;

	// Parent class still owns SpringArm/FollowCamera. We keep them around but
	// the PlayerController switches ViewTarget to a level-placed top-down
	// camera in BeginPlay, so this character's camera is never used.
	if (UCameraComponent* Cam = GetFollowCamera())
	{
		Cam->bAutoActivate = false;
	}
	if (USpringArmComponent* Boom = GetCameraBoom())
	{
		Boom->bDoCollisionTest = false;
		Boom->bUsePawnControlRotation = false;
	}

	// Players never collide with each other.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void AD1BomberCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void AD1BomberCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Run on both server and clients so each side's capsule sweep matches.
	// (Client-side movement prediction has its own MoveIgnoreActors list.)
	UpdateIgnoredBombs();
}

void AD1BomberCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AD1BomberCharacter, bIsInvulnerable);
}

void AD1BomberCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (PlaceBombAction)
		{
			EIC->BindAction(PlaceBombAction, ETriggerEvent::Started, this, &AD1BomberCharacter::ServerTryPlaceBomb);
		}
	}
}

void AD1BomberCharacter::ServerTryPlaceBomb_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	if (ActiveBomb.IsValid())
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

	AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && !PS->bIsAlive)
	{
		return;
	}

	const FIntPoint Cell = UD1BomberGridLibrary::WorldToCell(GetActorLocation());
	if (!UD1BomberGridLibrary::IsInsideGrid(Cell))
	{
		return;
	}
	if (GS && GS->IsWallCell(Cell))
	{
		return;
	}

	const FVector SpawnLoc = UD1BomberGridLibrary::CellToWorldCenter(Cell, 50.f);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AD1Bomb* Bomb = GetWorld()->SpawnActor<AD1Bomb>(BombClass, SpawnLoc, FRotator::ZeroRotator, Params);
	if (!Bomb)
	{
		return;
	}

	Bomb->Initialize(PS);
	ActiveBomb = Bomb;
	// The bomb itself registers every overlapping character (owner included)
	// into their IgnoredBombs set during its BeginPlay, so we don't need to do
	// that here. We only track the active bomb slot.
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

	// "Out of bomb area" check: the capsule must be fully clear of the bomb's
	// box collision before we re-enable blocking. Using horizontal distance
	// against (capsule radius + bomb half-extent + small margin) so when we
	// flip IgnoreActorWhenMoving back to false, the capsule is not still
	// penetrating the box (which would cause a snap-out).
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

void AD1BomberCharacter::AddIgnoredBomb(AD1Bomb* Bomb)
{
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

void AD1BomberCharacter::NotifyBombDestroyed(AD1Bomb* Bomb)
{
	if (ActiveBomb.Get() == Bomb)
	{
		ActiveBomb.Reset();
	}
	if (Bomb)
	{
		IgnoredBombs.Remove(Bomb);
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			Cap->IgnoreActorWhenMoving(Bomb, false);
		}
	}
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

void AD1BomberCharacter::EndInvulnerability()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsInvulnerable = false;
	OnRep_Invulnerable();
}

void AD1BomberCharacter::OnRep_Invulnerable()
{
	if (bIsInvulnerable)
	{
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

void AD1BomberCharacter::TickBlink()
{
	bBlinkVisible = !bBlinkVisible;
	if (USkeletalMeshComponent* SK = GetMesh())
	{
		SK->SetVisibility(bBlinkVisible);
	}
}

void AD1BomberCharacter::HandleDeath()
{
	if (USkeletalMeshComponent* SK = GetMesh())
	{
		SK->SetVisibility(false);
	}
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->DisableMovement();
	}
	GetWorldTimerManager().ClearTimer(InvulnTimerHandle);
	GetWorldTimerManager().ClearTimer(BlinkTimerHandle);
}

void AD1BomberCharacter::DoMove(float Right, float Forward)
{
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

void AD1BomberCharacter::DoLook(float /*Yaw*/, float /*Pitch*/)
{
	// Top-down camera is fixed; no per-player look.
}
