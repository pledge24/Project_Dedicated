// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1DedicatedServerSubsystem.h"
#include "Core/D1LogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

void UD1DedicatedServerSubsystem::BeginShutdownWatch(float InGraceSec)
{
	// PIE/Listen 서버는 에디터를 죽이면 안 됨 — 실 DS에서만 자가 종료.
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	// 재호출로 Elapsed를 되돌리면 하드캡이 무한 연장된다 — 첫 호출의 유예만 유효.
	if (bWatchStarted)
	{
		return;
	}
	bWatchStarted = true;

	GraceSec = InGraceSec;
	Elapsed = 0.f;
	GetWorld()->GetTimerManager().SetTimer(
		WatchTimerHandle, this, &UD1DedicatedServerSubsystem::TickWatch, 1.f, /*bLoop=*/true);
	UE_LOG(LogD1, Log, TEXT("[Match] 종료 감시 시작 — 전원 퇴장 또는 %.0fs 후 DS 종료"), GraceSec);
}

void UD1DedicatedServerSubsystem::TickWatch()
{
	// 클라들이 ClientTravel로 빠지면 PlayerController가 사라진다. 0이면 정상 종료.
	int32 NumPlayers = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->Get())
		{
			++NumPlayers;
		}
	}

	if (NumPlayers <= 0)
	{
		UE_LOG(LogD1, Log, TEXT("[Match] 전원 퇴장 — DS 종료"));
		RequestExit();
		return;
	}

	Elapsed += 1.f;
	if (Elapsed >= GraceSec)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 종료 하드캡(%.0fs) 도달 — 잔류 클라 무시하고 DS 종료"), GraceSec);
		RequestExit();
	}
}

void UD1DedicatedServerSubsystem::RequestExit()
{
	GetWorld()->GetTimerManager().ClearTimer(WatchTimerHandle);
	UE_LOG(LogD1, Log, TEXT("[Match] DS 프로세스 종료 요청(RequestExit)"));
	FPlatformMisc::RequestExit(false);
}
