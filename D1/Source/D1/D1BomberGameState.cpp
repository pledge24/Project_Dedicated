// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameState.h"
#include "Net/UnrealNetwork.h"

AD1BomberGameState::AD1BomberGameState()
{
	MatchPhase = EBomberMatchPhase::Waiting;
	MatchStartServerTime = 0.0f;
	MatchDurationSec = 300.0f; // 5분
}

void AD1BomberGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberGameState, MatchPhase);
	DOREPLIFETIME(AD1BomberGameState, WallCells);
	DOREPLIFETIME(AD1BomberGameState, MatchStartServerTime);
	DOREPLIFETIME(AD1BomberGameState, MatchDurationSec);
}

bool AD1BomberGameState::IsWallCell(const FIntPoint& Cell) const
{
	return WallCells.Contains(Cell);
}

float AD1BomberGameState::GetRemainingTimeSec() const
{
	// 시작 전: 풀 시간.
	if (MatchPhase == EBomberMatchPhase::Waiting)
	{
		return MatchDurationSec;
	}
	// 종료 후: 0.
	if (MatchPhase == EBomberMatchPhase::Finished)
	{
		return 0.0f;
	}

	const float Elapsed = GetServerWorldTimeSeconds() - MatchStartServerTime;
	return FMath::Clamp(MatchDurationSec - Elapsed, 0.0f, MatchDurationSec);
}

void AD1BomberGameState::OnRep_MatchPhase()
{
	// 클라측 반응 자리 (UI, 입력 차단 등).
}
