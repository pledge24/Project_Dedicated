// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

AD1BomberCharacter::AD1BomberCharacter()
{
	USpringArmComponent* Boom = GetCameraBoom();
	Boom->TargetArmLength = 1500.f;
	Boom->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	Boom->bUsePawnControlRotation = false;
	Boom->bInheritPitch = false;
	Boom->bInheritYaw = false;
	Boom->bInheritRoll = false;
	Boom->bDoCollisionTest = false;
}
