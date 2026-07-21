// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "D1BotController.generated.h"

/** 봇전(Bot-Fill) 봇 컨트롤러(서버 전용). PlayerState를 얻어 PlayerArray에 편입 — 시작 게이트·승패·카드·결과가 전부 PlayerArray 기준. */
UCLASS()
class AD1BotController : public AAIController
{
	GENERATED_BODY()

public:
	AD1BotController();
};
