// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameState.h"
#include "D1BomberPlayerState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 BomberMaxSlots = 4;
}

AD1BomberGameState::AD1BomberGameState()
{
	MatchPhase = EBomberMatchPhase::Waiting;
	MatchStartServerTime = 0.0f;
	MatchDurationSec = 300.0f; // 5분
	GridSize = FIntPoint::ZeroValue;

	// 서버시간 복제 주기 기본 5초 → 0.5초. HUD 타이머 클라간 드리프트 완화.
	ServerWorldTimeSecondsUpdateFrequency = 0.5f;
}

void AD1BomberGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberGameState, MatchPhase);
	DOREPLIFETIME(AD1BomberGameState, GridSize);
	DOREPLIFETIME(AD1BomberGameState, WallCells);
	DOREPLIFETIME(AD1BomberGameState, SoftBlockCells);
	DOREPLIFETIME(AD1BomberGameState, MatchStartServerTime);
	DOREPLIFETIME(AD1BomberGameState, MatchDurationSec);
	DOREPLIFETIME(AD1BomberGameState, FinalResults);
}

void AD1BomberGameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);
	MarkPlayerCardsDirty();
}

void AD1BomberGameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);
	MarkPlayerCardsDirty();
}

bool AD1BomberGameState::IsWallCell(const FIntPoint& Cell) const
{
	return WallCells.Contains(Cell);
}

bool AD1BomberGameState::IsSoftBlockCell(const FIntPoint& Cell) const
{
	return SoftBlockCells.Contains(Cell);
}

bool AD1BomberGameState::IsInsideGrid(const FIntPoint& Cell) const
{
	return Cell.X >= 0 && Cell.X < GridSize.X && Cell.Y >= 0 && Cell.Y < GridSize.Y;
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

TArray<AD1BomberPlayerState*> AD1BomberGameState::GetPlayerStatesBySlot() const
{
	TArray<AD1BomberPlayerState*> BySlot;
	BySlot.Init(nullptr, BomberMaxSlots);

	for (APlayerState* PS : PlayerArray)
	{
		AD1BomberPlayerState* BomberPS = Cast<AD1BomberPlayerState>(PS);
		if (!BomberPS)
		{
			continue;
		}
		const int32 Idx = BomberPS->PlayerSlotIndex;
		if (BySlot.IsValidIndex(Idx))
		{
			BySlot[Idx] = BomberPS;
		}
	}
	
	return BySlot;
}

void AD1BomberGameState::MarkPlayerCardsDirty()
{
	OnPlayerCardsDirty.Broadcast();
}

void AD1BomberGameState::SetFinalResults(const TArray<FD1MatchResultEntry>& InResults)
{
	FinalResults = InResults;

	// OnRep은 서버 자신에게 안 불리므로(리슨 서버) 수동 브로드캐스트.
	if (HasAuthority())
	{
		OnMatchFinished.Broadcast();
	}
}

void AD1BomberGameState::RemoveSoftBlockCell(const FIntPoint& Cell)
{
	SoftBlockCells.Remove(Cell);
}

void AD1BomberGameState::OnRep_MatchPhase()
{
	// 클라측 반응 자리 (UI, 입력 차단 등).
}

void AD1BomberGameState::OnRep_FinalResults()
{
	OnMatchFinished.Broadcast();
}
