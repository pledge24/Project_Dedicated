// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberPlayerState.h"
#include "D1BomberGameState.h"
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
	OnRep_Lives(); // Listen Server 대응

	if (Lives <= 0)
	{
		bIsAlive = false;
		OnRep_bIsAlive(); // Listen Server 대응
		return true;
	}
	return false;
}

void AD1BomberPlayerState::SetPlayerSlotIndex(int32 NewIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	PlayerSlotIndex = NewIndex;
	OnRep_PlayerSlotIndex(); // Listen Server 대응
}

void AD1BomberPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();
	OnPlayerNameChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_Lives()
{
	OnLivesChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_bIsAlive()
{
	OnAliveStateChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_PlayerSlotIndex()
{
	OnSlotIndexChanged.Broadcast();

	// 컨테이너 위젯이 한 곳에서 카드 전체를 다시 그릴 수 있게 GameState 디스패처도 트리거.
	if (UWorld* World = GetWorld())
	{
		if (AD1BomberGameState* GS = World->GetGameState<AD1BomberGameState>())
		{
			GS->MarkPlayerCardsDirty();
		}
	}
}
