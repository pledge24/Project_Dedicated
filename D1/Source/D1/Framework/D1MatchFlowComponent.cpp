// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1MatchFlowComponent.h"

#include "Core/D1LogChannels.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchTypes.h"
#include "GameFramework/PlayerController.h"
#include "Network/BackendTypes.h"
#include "Network/D1DedicatedServerSubsystem.h"
#include "Network/D1MatchResultSubsystem.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* EndReasonToString(EBomberEndReason Reason)
	{
		switch (Reason)
		{
		case EBomberEndReason::Winner:      return TEXT("winner");
		case EBomberEndReason::Draw:        return TEXT("draw");
		case EBomberEndReason::TimeExpired: return TEXT("time_expired");
		default:                            return TEXT("abort");
		}
	}
}

UD1MatchFlowComponent::UD1MatchFlowComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UD1MatchFlowComponent::InitializeMatch(int32 InExpectedPlayers, float InWaitTimeoutSec, float InShutdownGraceSec,
	const FString& InMatchId, const FString& InMatchToken)
{
	if (!HasServerAuthority())
	{
		return;
	}

	ExpectedPlayerCount      = InExpectedPlayers;
	WaitForPlayersTimeoutSec = InWaitTimeoutSec;
	ShutdownGraceSec         = InShutdownGraceSec;
	CurrentMatchId           = InMatchId;
	CurrentMatchToken        = InMatchToken;

	// 시작 게이트: 예상 인원 0/1(PIE·솔로)이면 즉시 시작, 아니면 전원 입장(PostLogin) 또는 타임아웃까지 Waiting.
	if (ExpectedPlayerCount <= 1)
	{
		StartMatch();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			WaitForPlayersTimerHandle, this, &UD1MatchFlowComponent::OnWaitForPlayersTimeout,
			WaitForPlayersTimeoutSec, /*bLoop=*/false);
		UE_LOG(LogD1, Log, TEXT("[Match] 시작 게이트 대기 — 예상 %d명 (타임아웃 %.0fs)"),
			ExpectedPlayerCount, WaitForPlayersTimeoutSec);
	}
}

void UD1MatchFlowComponent::HandlePlayerJoined()
{
	// 이미 시작했거나 게이트 비활성(PIE·솔로)이면 시작 게이트 카운트 생략.
	if (!HasServerAuthority() || HasMatchStarted() || ExpectedPlayerCount <= 1)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

	int32 Connected = 0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (Cast<AD1BomberPlayerState>(PS))
		{
			++Connected;
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 입장 %d/%d"), Connected, ExpectedPlayerCount);

	if (Connected >= ExpectedPlayerCount)
	{
		StartMatch();
	}
}

void UD1MatchFlowComponent::StartMatch()
{
	if (HasMatchStarted())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(WaitForPlayersTimerHandle);
	}

	if (AD1BomberGameState* GS = GetBomberGameState())
	{
		GS->MatchStartServerTime = GS->GetServerWorldTimeSeconds();
		GS->MatchPhase = EBomberMatchPhase::Playing;

		if (World)
		{
			World->GetTimerManager().SetTimer(
				MatchTimerHandle, this, &UD1MatchFlowComponent::OnMatchTimeExpired,
				GS->MatchDurationSec, /*bLoop=*/false);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 매치 시작 (Playing)"));
}

void UD1MatchFlowComponent::OnWaitForPlayersTimeout()
{
	if (HasMatchStarted())
	{
		return;
	}
	UE_LOG(LogD1, Warning, TEXT("[Match] 시작 게이트 타임아웃 — 현재 인원으로 시작"));
	StartMatch();
}

void UD1MatchFlowComponent::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	if (!HasServerAuthority() || IsMatchEnded() || !DeadPS)
	{
		return;
	}

	EnsureAliveListInitialized();

	if (DeadPS->GetPlacement() <= 0)
	{
		// 등수 = 죽는 시점의 생존자 수(자기 포함).
		DeadPS->SetPlacement(AlivePlayerStates.Num());
		AlivePlayerStates.Remove(DeadPS);
		UE_LOG(LogD1, Log, TEXT("Player died: %s Placement=%d Remaining=%d"),
			*DeadPS->GetPlayerName(), DeadPS->GetPlacement(), AlivePlayerStates.Num());
	}

	if (AlivePlayerStates.Num() <= 1)
	{
		const bool bHasSurvivor = AlivePlayerStates.Num() == 1;
		AD1BomberPlayerState* Winner = bHasSurvivor ? AlivePlayerStates[0].Get() : DeadPS;
		EndMatchWithWinner(Winner, bHasSurvivor ? EBomberEndReason::Winner : EBomberEndReason::Draw);
	}
}

void UD1MatchFlowComponent::EnsureAliveListInitialized()
{
	if (AlivePlayerStates.Num() > 0)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* BPS = Cast<AD1BomberPlayerState>(PS))
		{
			// ApplyHit가 NotifyPlayerDied보다 먼저 bIsAlive를 꺼서, 첫 사망자가
			// 누락되면 등수가 1 모자람. 미랭크(Placement<=0) 기준으로 전원 포함.
			if (BPS->GetPlacement() <= 0)
			{
				AlivePlayerStates.Add(BPS);
			}
		}
	}
}

void UD1MatchFlowComponent::OnMatchTimeExpired()
{
	if (IsMatchEnded())
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// 생존자는 EndMatchWithWinner에서 공동 1위로 보정된다.
	EndMatchWithWinner(nullptr, EBomberEndReason::TimeExpired);
}

void UD1MatchFlowComponent::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason)
{
	if (IsMatchEnded())
	{
		return;
	}

	if (WinnerPS && WinnerPS->GetPlacement() <= 0)
	{
		WinnerPS->SetPlacement(1);
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (GS)
	{
		GS->MatchPhase = EBomberMatchPhase::Finished;
	}

	UE_LOG(LogD1, Log, TEXT("Match ended (%s). Winner=%s (Placement=%d)"),
		EndReasonToString(Reason),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->GetPlacement() : 0);

	UWorld* World = GetWorld();
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->DisableInput(PC);
			}
		}
	}

	if (!GS)
	{
		return;
	}

	// 최종 결과 스냅샷(UI 원자 복제) + 백엔드 보고용 수집을 한 번에.
	TArray<FD1MatchResultEntry> Entries;
	TArray<FMatchResultPlayer> ResultPlayers;
	Entries.Reserve(GS->PlayerArray.Num());
	ResultPlayers.Reserve(GS->PlayerArray.Num());
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			// 미배정 생존자(시간 만료/무승부)는 공동 1위로 보정 — 백엔드는 placement 1~N만 허용.
			if (B->GetPlacement() <= 0)
			{
				B->SetPlacement(1);
			}

			FD1MatchResultEntry Entry;
			Entry.Placement = B->GetPlacement();
			Entry.Nickname  = B->GetPlayerName();
			Entry.SlotIndex = B->GetPlayerSlotIndex();
			Entry.LivesLeft = B->GetLives();
			Entries.Add(Entry);

			FMatchResultPlayer RP;
			RP.UserId    = B->GetBackendUserId();
			RP.SlotIndex = B->GetPlayerSlotIndex();
			RP.Placement = B->GetPlacement();
			RP.LivesLeft = B->GetLives();
			ResultPlayers.Add(RP);
		}
	}

	// UI 표시용 결정적 순서: 등수 오름차순, 동률은 슬롯 순. (PlayerArray 순서는 비결정)
	Entries.Sort([](const FD1MatchResultEntry& A, const FD1MatchResultEntry& B)
	{
		return A.Placement != B.Placement ? A.Placement < B.Placement : A.SlotIndex < B.SlotIndex;
	});

	GS->SetFinalResults(Entries);

	// 백엔드가 띄운 DS일 때만 결과 보고(토큰 없으면 PIE/standalone → 스킵).
	if (!CurrentMatchToken.IsEmpty() && World)
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UD1MatchResultSubsystem* ResultClient = GI->GetSubsystem<UD1MatchResultSubsystem>())
			{
				const int32 DurationSec = FMath::Max(0,
					FMath::RoundToInt(GS->GetServerWorldTimeSeconds() - GS->MatchStartServerTime));
				ResultClient->ReportMatchResult(CurrentMatchId, CurrentMatchToken, GS->MapName,
					DurationSec, EndReasonToString(Reason), ResultPlayers);
			}
		}
	}

	// 클라들이 결과 화면 카운트다운 후 ClientTravel로 빠지면 DS가 스스로 종료.
	if (World)
	{
		if (UD1DedicatedServerSubsystem* DS = World->GetSubsystem<UD1DedicatedServerSubsystem>())
		{
			DS->BeginShutdownWatch(ShutdownGraceSec);
		}
	}
}

bool UD1MatchFlowComponent::HasMatchStarted() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->MatchPhase != EBomberMatchPhase::Waiting;
}

bool UD1MatchFlowComponent::IsMatchEnded() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->MatchPhase == EBomberMatchPhase::Finished;
}

AD1BomberGameState* UD1MatchFlowComponent::GetBomberGameState() const
{
	return Cast<AD1BomberGameState>(GetOwner());
}

bool UD1MatchFlowComponent::HasServerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
