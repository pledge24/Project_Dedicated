// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1Character.h"
#include "D1BomberCharacter.generated.h"

UCLASS(abstract)
class AD1BomberCharacter : public AD1Character
{
	GENERATED_BODY()

public:
	AD1BomberCharacter();

	/** Top-down: ignore controller rotation, move along world axes. */
	virtual void DoMove(float Right, float Forward) override;

	/** Top-down: disable look input from controllers. */
	virtual void DoLook(float Yaw, float Pitch) override;
};
