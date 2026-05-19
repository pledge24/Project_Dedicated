// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberPlayerState.h"
#include "Net/UnrealNetwork.h"

AD1BomberPlayerState::AD1BomberPlayerState()
{
	Lives = 3;
	bIsAlive = true;
	Placement = 0;
}

void AD1BomberPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberPlayerState, Lives);
	DOREPLIFETIME(AD1BomberPlayerState, bIsAlive);
	DOREPLIFETIME(AD1BomberPlayerState, Placement);
}

bool AD1BomberPlayerState::ApplyHit()
{
	if (!HasAuthority() || !bIsAlive)
	{
		return false;
	}

	Lives = FMath::Max(0, Lives - 1);
	if (Lives <= 0)
	{
		bIsAlive = false;
		return true;
	}
	return false;
}

void AD1BomberPlayerState::OnRep_Lives()
{
	// Hook for UI updates on clients. Bind from HUD/UMG in later step.
}
