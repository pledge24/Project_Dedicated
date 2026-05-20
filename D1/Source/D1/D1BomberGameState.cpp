// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameState.h"
#include "Net/UnrealNetwork.h"

AD1BomberGameState::AD1BomberGameState()
{
	MatchPhase = EBomberMatchPhase::Waiting;
}

void AD1BomberGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberGameState, MatchPhase);
	DOREPLIFETIME(AD1BomberGameState, WallCells);
}

bool AD1BomberGameState::IsWallCell(const FIntPoint& Cell) const
{
	return WallCells.Contains(Cell);
}

void AD1BomberGameState::OnRep_MatchPhase()
{
	// 클라측 반응 자리 (UI, 입력 차단 등).
}
