// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
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

	// Top-down: controller never rotates the character; movement steers it.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 500.f, 0.f);
		Move->MaxWalkSpeed = 500.f;
		Move->MinAnalogWalkSpeed = 20.f;
		Move->BrakingDecelerationWalking = 2000.f;
	}

	// Players never collide with each other.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(42.f, 96.f);
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

void AD1BomberCharacter::OnMoveInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	DoMove(Axis.X, Axis.Y);
}

void AD1BomberCharacter::ServerTryPlaceBomb_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	if (GetActiveBombCount() >= MaxBombCount)
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

	// One bomb per cell — block stacking on top of any existing bomb in the
	// world, not just our own. Two characters can occupy the same cell
	// (pawn-vs-pawn collision is disabled) and both can be in a bomb's
	// IgnoredBombs set, so without this check two players could double-stack.
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

	// ============ VALIDATION END =============

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
	ActiveBombs.Add(Bomb);
	// The bomb itself registers every overlapping character (owner included)
	// into their IgnoredBombs set during its BeginPlay, so we don't need to do
	// that here. We only track the active bomb slot.
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
	/** Server-only: register a bomb that the character is currently overlapping.
	 *  As long as the character stays in the bomb's cell, the capsule treats
	 *  the bomb as non-blocking. Once the character leaves the cell, the Tick
	 *  cleanup re-enables blocking so the bomb can't be re-entered. */
	
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
	// called by AD1Bomb when it detonates so the owner's slot frees up.
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
	// clean up after own death (mesh hide, collision off)
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

