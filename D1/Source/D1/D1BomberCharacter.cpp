// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

AD1BomberCharacter::AD1BomberCharacter()
{
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
