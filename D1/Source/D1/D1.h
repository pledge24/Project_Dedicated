// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogD1, Log, All);

/** Custom collision channel for bombs.
 *  Configured in DefaultEngine.ini under [/Script/Engine.CollisionProfile] with Name="Bomb". */
#define ECC_Bomb ECC_GameTraceChannel1