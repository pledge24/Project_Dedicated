// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "D1DsShutdownSubsystem.generated.h"

/**
 *  DS 프로세스 자가 종료. 매치 종료 후 전원 퇴장 또는 하드캡 도달 시 종료를 요청한다.
 *  실 DS에서만 동작(BeginShutdownWatch가 IsRunningDedicatedServer로 가드 — PIE/Listen은 no-op).
 */
UCLASS()
class UD1DsShutdownSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 서버 전용: 매치 종료 시 호출. 전원 퇴장 또는 GraceSec 경과 시 DS 프로세스를 종료. 실 DS 아니면 no-op.
	 * 멱등 — 결과 보고 확정과 보고 하드캡이 모두 도달할 수 있으므로 첫 호출의 유예만 유효하다.
	 */
	void BeginShutdownWatch(float InGraceSec);

private:
	void TickWatch();
	void RequestExit();

	FTimerHandle WatchTimerHandle;

	float ElapsedSec = 0.f;
	float GraceSec = 0.f;

	bool bWatchStarted = false;
};
