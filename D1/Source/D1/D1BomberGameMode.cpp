// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameMode.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

AD1BomberGameMode::AD1BomberGameMode()
{
	// stub
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
