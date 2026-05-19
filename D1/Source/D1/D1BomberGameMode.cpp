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

void AD1BomberGameMode::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	// Implemented in Step 7. Stub for now.
	if (DeadPS)
	{
		UE_LOG(LogD1, Log, TEXT("Player died: %s (Lives=%d)"),
			*DeadPS->GetPlayerName(), DeadPS->Lives);
	}
}

void AD1BomberGameMode::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS)
{
	// Implemented in Step 7. Stub for now.
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
