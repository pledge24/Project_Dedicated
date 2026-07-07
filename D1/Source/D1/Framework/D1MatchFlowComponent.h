// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "D1MatchFlowComponent.generated.h"

class AD1BomberGameState;
class AD1BomberPlayerState;
enum class EBomberEndReason : uint8;

/**
 *  매치 흐름 담당 컴포넌트 (GameState 부착·서버 전용).
 *  시작 게이트 → 사망 등수 → 승패 판정 → 결과 스냅샷/백엔드 보고 → DS 셧다운을 소유.
 *  복제 상태는 GameState가 계속 소유(MatchPhase/FinalResults 등), 여기는 로직·서버 상태만.
 */
UCLASS()
class UD1MatchFlowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UD1MatchFlowComponent();

	/** GameMode::BeginPlay가 cmdline 파싱·맵빌드 후 호출. 설정을 받고 시작 게이트를 arm. */
	void InitializeMatch(int32 InExpectedPlayers, float InWaitTimeoutSec, float InShutdownGraceSec,
		const FString& InMatchId, const FString& InMatchToken);

	/** GameMode::PostLogin이 호출. 예상 인원 도달 시 매치 시작. */
	void HandlePlayerJoined();

	/** 서버 전용: 사망 등록·등수 부여, 1명 남으면 매치 종료. PlayerState::ApplyHit이 GameState 경유로 호출. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	//~ 내부 흐름
	void StartMatch();
	void OnWaitForPlayersTimeout();
	void OnMatchTimeExpired();
	void EnsureAliveListInitialized();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);

	//~ 매치 생애 질의 — GameState의 MatchPhase 단일 출처
	bool HasMatchStarted() const;
	bool IsMatchEnded() const;

	/** 소유 GameState. 없으면 nullptr. */
	AD1BomberGameState* GetBomberGameState() const;

	/** 서버 권위 여부. 모든 진입점 방어 가드. */
	bool HasServerAuthority() const;

	//~ 서버 전용 런타임 상태
	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	FTimerHandle MatchTimerHandle;
	FTimerHandle WaitForPlayersTimerHandle;

	//~ GameMode가 InitializeMatch로 주입하는 매치 세션 값
	int32 ExpectedPlayerCount = 0;
	float WaitForPlayersTimeoutSec = 20.f;
	float ShutdownGraceSec = 30.f;
	FString CurrentMatchId;
	FString CurrentMatchToken;
};
