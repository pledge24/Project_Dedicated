// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Framework/D1MatchTypes.h"
#include "D1MatchFlowComponent.generated.h"

class AD1BomberGameState;
class AD1BomberPlayerState;
class UD1MatchResultSubsystem;

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

//~ 시작 게이트
public:
	/** 서버 전용: GameMode::BeginPlay가 cmdline 파싱·맵빌드 후 호출. 설정을 받고 시작 게이트를 arm. */
	void InitializeMatch(int32 InExpectedPlayerCount, float InWaitTimeoutSec, float InShutdownGraceSec,
		const FString& InMatchId, const FString& InMatchToken, const TArray<FD1JoinEntry>& InExpectedRoster);

	/** 서버 전용: GameMode::PostLogin이 호출. 예상 인원 도달 시 매치 시작. */
	void NotifyPlayerJoined();

private:
	void StartMatch();
	void OnWaitForPlayersTimeout();

	FTimerHandle WaitForPlayersTimerHandle;

	/** GameMode가 InitializeMatch로 주입. 시작 정원(0/1=즉시)과 게이트 타임아웃. */
	int32 ExpectedPlayerCount = 0;
	float WaitForPlayersTimeoutSec = 20.f;

	/**
	 * -Roster= 로 온 휴먼 명단(GameMode가 주입). 결과 보고 시 한 번도 입장하지 않은 유저를
	 * 찾아내는 기준 — 백엔드 roster와 인원이 어긋나면 매치 전체 결과가 거부된다.
	 * 봇전 봇은 -Bots= 로 따로 오고 PlayerArray에 편입되므로 여기 없다.
	 */
	TArray<FD1JoinEntry> ExpectedRoster;

//~ 사망·등수
public:
	/** 서버 전용: 사망 기록. 등수·종료 판정은 다음 틱 배치 평가로 미룸. PlayerState::ApplyHit이 GameState 경유로 호출. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

	/** 서버 전용: 탈주 확정 통지(PlayerRemovalComp가 등수 부여 후 호출). 생존 목록 제외 + 다음 틱 종료 판정 예약. */
	void NotifyPlayerLeft(AD1BomberPlayerState* LeftPS);

private:
	void EnsureAliveListInitialized();
	/** 다음 틱 종료 평가 예약(중복 예약 방지). */
	void RequestEndEvaluation();
	/** 다음 틱 1회 실행: 배치 등수 확정 후 생존자 수로 종료 판정. */
	void EvaluateEndCondition();
	/** 대기 배치 전원에 공동 등수 부여 후 비움. 동시 사망 = 공동 등수. */
	void FlushPendingDeaths();

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	/** 이번 프레임에 사망 기록된 배치 — 다음 틱 평가에서 공동 등수 부여 후 비움. */
	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> PendingDeadBatch;

	/** 다음 틱 종료 평가 예약됨(프레임 내 다중 사망 → 평가 1회). */
	bool bEndEvaluationPending = false;

//~ 매치 종료·셧다운
private:
	void OnMatchTimeExpired();
	/** 종료 확정: 등수 보정·페이즈 전이는 여기서, 결과 두 배열 조립은 D1MatchSettlement에 위임. */
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);
	/** 결과 보고가 확정됐거나 하드캡에 걸렸을 때 DS 셧다운 감시 시작. 선착순 1회만 유효(감시가 멱등). */
	void BeginShutdownAfterReport();

	FTimerHandle MatchTimerHandle;
	FTimerHandle ResultReportHardCapTimerHandle;

	/** GameMode가 InitializeMatch로 주입. 셧다운 유예와 결과 POST 인증값. */
	float ShutdownGraceSec = 30.f;
	FString CurrentMatchId;
	FString CurrentMatchToken;

	/**
	 * 결과 보고 확정을 기다리는 상한. 넘으면 보고를 포기하고 종료한다.
	 * 재시도 백오프 누적(30초)에 요청당 HTTP 타임아웃(기본 15초)이 곱해질 수 있어 그보다 넉넉해야 한다 —
	 * 짧으면 재시도가 끝나기 전에 DS가 죽어 결과가 유실된다. DS 최대 수명(15분) 안이라 포트 점유 문제는 없다.
	 */
	float ResultReportHardCapSec = 120.f;

//~ 상태 질의
private:
	/** GameState의 MatchPhase 단일 출처. */
	bool HasMatchStarted() const;
	bool IsMatchEnded() const;

	/** 소유 GameState. 없으면 nullptr. */
	AD1BomberGameState* GetBomberGameState() const;

	/** 결과 보고 Subsystem. 토큰 없으면(PIE/standalone) nullptr — 호출측은 보고 스킵. */
	UD1MatchResultSubsystem* GetResultClient() const;

	/** 서버 권위 여부. 모든 진입점 방어 가드. */
	bool HasServerAuthority() const;
};
