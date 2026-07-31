// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchFlowComponent.h"
#include "Net/UnrealNetwork.h"


AD1BomberGameState::AD1BomberGameState()
{
	// 서버시간 복제 주기 기본 5초 → 0.5초. HUD 타이머 클라간 드리프트 완화.
	ServerWorldTimeSecondsUpdateFrequency = 0.5f;

	// 매치 흐름 로직은 컴포넌트로 위임(서버 전용 실행). GameState 수명과 동일.
	MatchFlowComp = CreateDefaultSubobject<UD1MatchFlowComponent>(TEXT("MatchFlow"));
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
	DOREPLIFETIME(AD1BomberGameState, LeftPlayerCards);
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

void AD1BomberGameState::SetGridData(const FIntPoint& InGridSize, const TArray<FIntPoint>& InWallCells,
	const TArray<FIntPoint>& InSoftBlockCells, const FString& InMapName)
{
	if (!HasAuthority())
	{
		return;
	}

	GridSize = InGridSize;
	WallCells = InWallCells;
	SoftBlockCells = InSoftBlockCells;
	MapName = InMapName;
}

void AD1BomberGameState::RemoveSoftBlockCell(const FIntPoint& Cell)
{
	SoftBlockCells.Remove(Cell);
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

void AD1BomberGameState::SetMatchStartServerTime(float ServerTime)
{
	if (!HasAuthority())
	{
		return;
	}
	MatchStartServerTime = ServerTime;
}

TArray<AD1BomberPlayerState*> AD1BomberGameState::GetPlayerStatesBySlot() const
{
	TArray<AD1BomberPlayerState*> BySlot;
	BySlot.Init(nullptr, D1MaxPlayerSlots);

	for (APlayerState* PS : PlayerArray)
	{
		AD1BomberPlayerState* BomberPS = Cast<AD1BomberPlayerState>(PS);
		if (!BomberPS)
		{
			continue;
		}
		const int32 Idx = BomberPS->GetPlayerSlotIndex();
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

bool AD1BomberGameState::IsSlotLeft(int32 SlotIndex, FString& OutNickname) const
{
	for (const FD1LeftPlayerCard& Card : LeftPlayerCards)
	{
		if (Card.SlotIndex == SlotIndex)
		{
			OutNickname = Card.Nickname;
			return true;
		}
	}

	OutNickname.Reset();
	return false;
}

void AD1BomberGameState::MarkSlotLeft(int32 SlotIndex, const FString& Nickname)
{
	if (!HasAuthority() || SlotIndex < 0)
	{
		return;
	}

	// 슬롯당 탈주 1회 — 재기록 방지.
	const bool bAlready = LeftPlayerCards.ContainsByPredicate(
		[SlotIndex](const FD1LeftPlayerCard& Card) { return Card.SlotIndex == SlotIndex; });
	if (bAlready)
	{
		return;
	}

	FD1LeftPlayerCard Card;
	Card.SlotIndex = SlotIndex;
	Card.Nickname  = Nickname;
	LeftPlayerCards.Add(Card);

	// OnRep은 서버 자신에게 안 불림(리슨/DS) → 수동 호출로 카드 갱신.
	OnRep_LeftPlayerCards();
}

void AD1BomberGameState::OnRep_LeftPlayerCards()
{
	MarkPlayerCardsDirty();
}

void AD1BomberGameState::SetFinalResults(const TArray<FD1MatchResultEntry>& InResults)
{
	// 복제 배열 쓰기 자체를 서버 권위로 가드(브로드캐스트만 가드하면 클라 로컬 사본이 오염될 수 있다).
	if (!HasAuthority())
	{
		return;
	}

	FinalResults = InResults;

	// OnRep은 서버 자신에게 안 불리므로(리슨 서버) 수동 브로드캐스트.
	OnMatchFinished.Broadcast();
}

void AD1BomberGameState::SetMatchPhase(EBomberMatchPhase NewPhase)
{
	if (!HasAuthority())
	{
		return;
	}
	MatchPhase = NewPhase;
}

void AD1BomberGameState::OnRep_FinalResults()
{
	OnMatchFinished.Broadcast();
}
