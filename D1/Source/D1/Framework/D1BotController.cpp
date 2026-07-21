// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BotController.h"

AD1BotController::AD1BotController()
{
	// AIController 기본값 false — 켜야 PlayerState가 생성돼 PlayerArray에 잡힌다(Lyra 봇 생성 패턴).
	bWantsPlayerState = true;
}
