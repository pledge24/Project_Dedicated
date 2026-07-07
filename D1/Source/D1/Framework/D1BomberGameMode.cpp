// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Network/D1DedicatedServerSubsystem.h"
#include "Systems/Map/D1MapBuilder.h"
#include "Systems/Map/D1MapData.h"
#include "Framework/D1MatchTypes.h"
#include "Core/D1LogChannels.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Network/D1MatchResultSubsystem.h"

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

	// 이름순 정렬 — 슬롯 인덱스 일관성 확보.
	void GatherSortedPlayerStarts(const UObject* WorldContext, TArray<AActor*>& OutStarts)
	{
		UGameplayStatics::GetAllActorsOfClass(WorldContext, APlayerStart::StaticClass(), OutStarts);
		OutStarts.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetName() < B.GetName();
		});
	}

	// PlayerStartTag가 "0"~"3"이면 그 슬롯, 아니면 DefaultSlot 유지.
	int32 ResolveSlotFromTag(const AActor* Start, int32 DefaultSlot)
	{
		if (const APlayerStart* PS = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PS->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 Parsed = FCString::Atoi(*TagStr);
				if (Parsed >= 0 && Parsed <= 3)
				{
					return Parsed;
				}
			}
		}
		return DefaultSlot;
	}
}

AD1BomberGameMode::AD1BomberGameMode()
{
	GameStateClass = AD1BomberGameState::StaticClass();
	PlayerStateClass = AD1BomberPlayerState::StaticClass();
}

void AD1BomberGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 백엔드가 spawn 시 주입한 매치 식별자/토큰/예상 인원. 둘 다 없으면 PIE/standalone(결과 POST 스킵).
	FParse::Value(FCommandLine::Get(), TEXT("MatchId="), CurrentMatchId);
	FParse::Value(FCommandLine::Get(), TEXT("MatchToken="), CurrentMatchToken);
	FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayerCount);
	if (!CurrentMatchId.IsEmpty())
	{
		UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d"),
			*CurrentMatchId, CurrentMatchToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"), ExpectedPlayerCount);
	}

	// 백엔드 권위 roster 주입({token}:{userId};…). InitNewPlayer가 ?join= 토큰으로 신원을 확정한다.
	FString RosterStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("Roster="), RosterStr) && !RosterStr.IsEmpty())
	{
		TArray<FString> Entries;
		RosterStr.ParseIntoArray(Entries, TEXT(";"), /*CullEmpty=*/true);
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.ParseIntoArray(Parts, TEXT(":"), /*CullEmpty=*/true);
			if (Parts.Num() == 2)
			{
				FD1JoinEntry JE;
				JE.UserId = FCString::Atoi64(*Parts[1]);
				JoinRoster.Add(Parts[0], JE);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] roster 항목 형식 오류(무시): '%s'"), *Entry);
			}
		}
		UE_LOG(LogD1, Log, TEXT("[Match] roster 주입 %d명"), JoinRoster.Num());
	}

	// 맵 빌드는 전용 헬퍼로 위임(GameMode는 config만 보유·전달).
	FD1MapBuildConfig MapCfg;
	MapCfg.DefaultMap  = MapData;
	MapCfg.BlockZ      = BlockZ;
	MapCfg.PickupClass = PowerupPickupClass;
	MapCfg.DropChance  = PowerupDropChance;
	MapCfg.FireWeight  = FireDropWeight;
	MapCfg.BombWeight  = BombDropWeight;
	MapCfg.SpeedWeight = SpeedDropWeight;
	MapCfg.PowerupZ    = PowerupZ;
	FString MapErr;
	if (!UD1MapBuilder::Build(GetWorld(), GetGameState<AD1BomberGameState>(), MapCfg, MapErr))
	{
		UE_LOG(LogD1, Error, TEXT("[Map] 빌드 실패: %s"), *MapErr);
	}

	// 시작 게이트: 예상 인원 0/1(PIE·솔로)이면 즉시 시작, 아니면 전원 입장(PostLogin) 또는 타임아웃까지 Waiting.
	if (ExpectedPlayerCount <= 1)
	{
		StartMatch();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			WaitForPlayersTimerHandle, this, &AD1BomberGameMode::OnWaitForPlayersTimeout,
			WaitForPlayersTimeoutSec, /*bLoop=*/false);
		UE_LOG(LogD1, Log, TEXT("[Match] 시작 게이트 대기 — 예상 %d명 (타임아웃 %.0fs)"),
			ExpectedPlayerCount, WaitForPlayersTimeoutSec);
	}
}

FString AD1BomberGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	AD1BomberPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<AD1BomberPlayerState>() : nullptr;
	if (PS)
	{
		// 클라가 접속시 가져온 토큰(travel URL의 ?join=)이 백엔드가 준 roster에 있는지 확인.
		// 토큰이 roster에 존재 O -> PS에 UserId 넣어준다.
		// 토큰이 roster에 존재 X -> 해당 클라는 신용하지 않는다.(또는 PIE/StandAlone으로 판단)
		const FString JoinToken = UGameplayStatics::ParseOption(Options, TEXT("join"));
		if (!JoinToken.IsEmpty())
		{
			if (const FD1JoinEntry* Entry = JoinRoster.Find(JoinToken))
			{
				PS->SetBackendUserId(Entry->UserId);
				UE_LOG(LogD1, Log, TEXT("[Match] InitNewPlayer %s userId=%lld (roster)"),
					*PS->GetPlayerName(), Entry->UserId);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] InitNewPlayer %s — join 토큰이 roster에 없음"), *PS->GetPlayerName());
			}
		}
	}

	return Result;
}

AActor* AD1BomberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});

	TArray<AActor*> AllStarts;
	GatherSortedPlayerStarts(this, AllStarts);

	// 위치 선정은 순수 랜덤(사용자 지시): 안 쓴 Start 집합에서 균등 랜덤 1개를 뽑는다.
	// 비복원(미사용만 후보)이라 한 매치의 슬롯 0~3은 항상 유일 → DB UNIQUE(match,slot)와 안전.
	TArray<AActor*> FreeStarts;
	FreeStarts.Reserve(AllStarts.Num());
	// 미사용 Start만 후보로. (UsedStarts는 위에서 invalid 제거됨 → Contains 안전)
	for (AActor* Start : AllStarts)
	{
		if (!UsedStarts.Contains(Start))
		{
			FreeStarts.Add(Start);
		}
	}

	if (FreeStarts.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	AActor* Chosen = FreeStarts[FMath::RandHelper(FreeStarts.Num())];

	// 슬롯 라벨 = 뽑힌 Start의 PlayerStartTag(없으면 정렬 인덱스). 스폰 코너·카드 자리가 함께 결정된다.
	if (AD1BomberPlayerState* BomberPS = Player ? Player->GetPlayerState<AD1BomberPlayerState>() : nullptr)
	{
		const int32 SlotIndex = ResolveSlotFromTag(Chosen, AllStarts.IndexOfByKey(Chosen));
		BomberPS->SetPlayerSlotIndex(SlotIndex);
		UE_LOG(LogD1, Log, TEXT("Slot %d -> Start %s (random) for %s"),
			SlotIndex, *Chosen->GetName(), *BomberPS->GetPlayerName());
	}

	UsedStarts.Add(Chosen);
	return Chosen;
}

void AD1BomberGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// 이미 시작했거나 게이트 비활성(PIE·솔로)이면 시작 게이트 카운트 생략.
	if (HasMatchStarted() || ExpectedPlayerCount <= 1)
	{
		return;
	}

	int32 Connected = 0;
	if (const AGameStateBase* GS = GameState)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (Cast<AD1BomberPlayerState>(PS))
			{
				++Connected;
			}
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 입장 %d/%d"), Connected, ExpectedPlayerCount);

	if (Connected >= ExpectedPlayerCount)
	{
		StartMatch();
	}
}

void AD1BomberGameMode::Logout(AController* Exiting)
{
	// 떠난 플레이어가 점유했던 PlayerStart를 해제 → fallback(순번) 경로 슬롯 누수 방지.
	// 권위 슬롯 경로에선 슬롯이 고정이라 no-op이어도 무방.
	if (Exiting && Exiting->StartSpot.IsValid())
	{
		UsedStarts.Remove(Exiting->StartSpot);
	}
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	Super::Logout(Exiting);
}

void AD1BomberGameMode::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	if (IsMatchEnded() || !DeadPS)
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

void AD1BomberGameMode::StartMatch()
{
	if (HasMatchStarted())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WaitForPlayersTimerHandle);

	if (AD1BomberGameState* BomberGS = GetGameState<AD1BomberGameState>())
	{
		BomberGS->MatchStartServerTime = BomberGS->GetServerWorldTimeSeconds();
		BomberGS->MatchPhase = EBomberMatchPhase::Playing;

		GetWorldTimerManager().SetTimer(
			MatchTimerHandle, this, &AD1BomberGameMode::OnMatchTimeExpired,
			BomberGS->MatchDurationSec, /*bLoop=*/false);
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 매치 시작 (Playing)"));
}

void AD1BomberGameMode::OnWaitForPlayersTimeout()
{
	if (HasMatchStarted())
	{
		return;
	}
	UE_LOG(LogD1, Warning, TEXT("[Match] 시작 게이트 타임아웃 — 현재 인원으로 시작"));
	StartMatch();
}

void AD1BomberGameMode::OnMatchTimeExpired()
{
	if (IsMatchEnded())
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// 생존자는 EndMatchWithWinner에서 공동 1위로 보정된다.
	EndMatchWithWinner(nullptr, EBomberEndReason::TimeExpired);
}

void AD1BomberGameMode::EnsureAliveListInitialized()
{
	if (AlivePlayerStates.Num() > 0)
	{
		return;
	}
	if (AGameStateBase* GSB = GameState)
	{
		for (APlayerState* PS : GSB->PlayerArray)
		{
			if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
			{
				// ApplyHit가 NotifyPlayerDied보다 먼저 bIsAlive를 꺼서, 첫 사망자가
				// 누락되면 등수가 1 모자람. 미랭크(Placement<=0) 기준으로 전원 포함.
				if (B->GetPlacement() <= 0)
				{
					AlivePlayerStates.Add(B);
				}
			}
		}
	}
}

void AD1BomberGameMode::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason)
{
	if (IsMatchEnded())
	{
		return;
	}

	if (WinnerPS && WinnerPS->GetPlacement() <= 0)
	{
		WinnerPS->SetPlacement(1);
	}

	AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	if (GS)
	{
		GS->MatchPhase = EBomberMatchPhase::Finished;
	}

	UE_LOG(LogD1, Log, TEXT("Match ended (%s). Winner=%s (Placement=%d)"),
		EndReasonToString(Reason),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->GetPlacement() : 0);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->DisableInput(PC);
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
	if (!CurrentMatchToken.IsEmpty())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UD1MatchResultSubsystem* ResultClient = GI->GetSubsystem<UD1MatchResultSubsystem>())
			{
				const int32 DurationSec = FMath::Max(0,
					FMath::RoundToInt(GS->GetServerWorldTimeSeconds() - GS->MatchStartServerTime));
				ResultClient->ReportMatchResult(CurrentMatchId, CurrentMatchToken, GetWorld()->GetMapName(),
					DurationSec, EndReasonToString(Reason), ResultPlayers);
			}
		}
	}

	// 클라들이 결과 화면 카운트다운 후 ClientTravel로 빠지면 DS가 스스로 종료.
	if (UD1DedicatedServerSubsystem* DS = GetWorld()->GetSubsystem<UD1DedicatedServerSubsystem>())
	{
		DS->BeginShutdownWatch(ShutdownGraceSec);
	}
}

bool AD1BomberGameMode::HasMatchStarted() const
{
	const AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	return GS && GS->MatchPhase != EBomberMatchPhase::Waiting;
}

bool AD1BomberGameMode::IsMatchEnded() const
{
	const AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	return GS && GS->MatchPhase == EBomberMatchPhase::Finished;
}
