// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Menu/D1MenuGameMode.h"

#include "GameFramework/SpectatorPawn.h"
#include "Framework/Menu/D1MenuPlayerController.h"

AD1MenuGameMode::AD1MenuGameMode()
{
	// 메뉴에는 캐릭터를 스폰하지 않는다. SpectatorPawn 하나로 충분.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	PlayerControllerClass = AD1MenuPlayerController::StaticClass();
}
