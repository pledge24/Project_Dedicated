// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberPlayerState.h"
#include "Net/UnrealNetwork.h"

AD1BomberPlayerState::AD1BomberPlayerState()
{
	Lives = 3;
	bIsAlive = true;
	Placement = 0;
	PlayerSlotIndex = -1;
}

void AD1BomberPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberPlayerState, Lives);
	DOREPLIFETIME(AD1BomberPlayerState, bIsAlive);
	DOREPLIFETIME(AD1BomberPlayerState, Placement);
	DOREPLIFETIME(AD1BomberPlayerState, PlayerSlotIndex);
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
	// 클라 UI 갱신 자리. 추후 HUD/UMG에서 바인딩.
}
