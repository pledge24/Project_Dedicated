// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameMode.h"
#include "D1BomberGameState.h"
#include "D1BomberPlayerState.h"
#include "D1BomberGridLibrary.h"
#include "D1WallBlock.h"
#include "D1.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

AD1BomberGameMode::AD1BomberGameMode()
{
	GameStateClass = AD1BomberGameState::StaticClass();
	PlayerStateClass = AD1BomberPlayerState::StaticClass();
}

void AD1BomberGameMode::BeginPlay()
{
	Super::BeginPlay();

	PopulateWallData();

	if (AD1BomberGameState* BomberGS = GetGameState<AD1BomberGameState>())
	{
		BomberGS->MatchStartServerTime = BomberGS->GetServerWorldTimeSeconds();
		BomberGS->MatchPhase = EBomberMatchPhase::Playing;

		// 매치 제한시간 만료 콜백.
		GetWorldTimerManager().SetTimer(
			MatchTimerHandle, this, &AD1BomberGameMode::OnMatchTimeExpired,
			BomberGS->MatchDurationSec, /*bLoop=*/false);
	}
}

AActor* AD1BomberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	TArray<AActor*> AllStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), AllStarts);

	// 액터 이름 알파벳 정렬 — 슬롯 인덱스 일관성 확보.
	AllStarts.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});

	for (int32 i = 0; i < AllStarts.Num(); ++i)
	{
		AActor* Start = AllStarts[i];

		bool bAlreadyUsed = false;
		for (const TWeakObjectPtr<AActor>& Used : UsedStarts)
		{
			if (Used.Get() == Start)
			{
				bAlreadyUsed = true;
				break;
			}
		}
		if (bAlreadyUsed)
		{
			continue;
		}

		// 슬롯 인덱스 결정: PlayerStartTag가 "0"~"3"이면 그 값, 아니면 정렬 인덱스.
		int32 SlotIndex = i;
		if (APlayerStart* PS = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PS->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 Parsed = FCString::Atoi(*TagStr);
				if (Parsed >= 0 && Parsed <= 3)
				{
					SlotIndex = Parsed;
				}
			}
		}

		// PlayerState에 슬롯 부여.
		if (Player)
		{
			if (AD1BomberPlayerState* BomberPS = Player->GetPlayerState<AD1BomberPlayerState>())
			{
				BomberPS->SetPlayerSlotIndex(SlotIndex);
				UE_LOG(LogD1, Log, TEXT("Assigned PlayerSlotIndex=%d to %s (Start=%s)"),
					SlotIndex, *BomberPS->GetPlayerName(), *Start->GetName());
			}
		}

		UsedStarts.Add(Start);
		return Start;
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

void AD1BomberGameMode::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	if (bMatchEnded || !DeadPS)
	{
		return;
	}

	EnsureAliveListInitialized();

	if (DeadPS->Placement <= 0)
	{
		// 등수 = 죽는 시점의 생존자 수(자기 포함).
		DeadPS->Placement = AlivePlayerStates.Num();
		AlivePlayerStates.Remove(DeadPS);
		UE_LOG(LogD1, Log, TEXT("Player died: %s Placement=%d Remaining=%d"),
			*DeadPS->GetPlayerName(), DeadPS->Placement, AlivePlayerStates.Num());
	}

	if (AlivePlayerStates.Num() <= 1)
	{
		AD1BomberPlayerState* Winner = (AlivePlayerStates.Num() == 1)
			? AlivePlayerStates[0].Get()
			: DeadPS;
		EndMatchWithWinner(Winner);
	}
}

void AD1BomberGameMode::PopulateWallData()
{
	AD1BomberGameState* BomberGS = GetGameState<AD1BomberGameState>();
	if (!BomberGS)
	{
		UE_LOG(LogD1, Warning, TEXT("BomberGameMode: GameState is not AD1BomberGameState; skipping wall data"));
		return;
	}

	TArray<FIntPoint> Cells;
	UD1BomberGridLibrary::BuildDefaultWallCells(Cells);
	BomberGS->WallCells = Cells;

	UE_LOG(LogD1, Log, TEXT("BomberGameMode: populated %d wall cells"), Cells.Num());
}

void AD1BomberGameMode::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS)
{
	if (bMatchEnded)
	{
		return;
	}
	bMatchEnded = true;

	if (WinnerPS && WinnerPS->Placement <= 0)
	{
		WinnerPS->Placement = 1;
	}

	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		GS->MatchPhase = EBomberMatchPhase::Finished;
	}

	UE_LOG(LogD1, Log, TEXT("Match ended. Winner=%s (Placement=%d)"),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->Placement : 0);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->DisableInput(PC);
		}
	}
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
				if (B->bIsAlive)
				{
					AlivePlayerStates.Add(B);
				}
			}
		}
	}
}

void AD1BomberGameMode::OnMatchTimeExpired()
{
	if (bMatchEnded)
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// placement 룰은 추후 정의. 일단 종료만.
	EndMatchWithWinner(nullptr);
}
