// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "D1DedicatedServerSubsystem.generated.h"

/**
 *  Dedicated Server 수명 관리. 매치 종료 후 전원 퇴장 또는 하드캡 도달 시 프로세스를 자가 종료한다.
 *  실 DS에서만 동작(BeginShutdownWatch가 IsRunningDedicatedServer로 가드 — PIE/Listen은 no-op).
 */
UCLASS()
class UD1DedicatedServerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 매치 종료 시 호출(서버). 전원 퇴장 또는 GraceSec 경과 시 DS 프로세스를 종료. 실 DS 아니면 no-op. */
	void BeginShutdownWatch(float InGraceSec);

private:
	void TickWatch();
	void RequestExit();

	FTimerHandle WatchTimerHandle;

	float Elapsed = 0.f;
	float GraceSec = 0.f;
};
