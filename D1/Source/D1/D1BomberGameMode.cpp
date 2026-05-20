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
		BomberGS->MatchPhase = EBomberMatchPhase::Playing;
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

AActor* AD1BomberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	TArray<AActor*> AllStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), AllStarts);

	for (AActor* Start : AllStarts)
	{
		bool bAlreadyUsed = false;
		for (const TWeakObjectPtr<AActor>& Used : UsedStarts)
		{
			if (Used.Get() == Start)
			{
				bAlreadyUsed = true;
				break;
			}
		}

		if (!bAlreadyUsed)
		{
			UsedStarts.Add(Start);
			return Start;
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}
