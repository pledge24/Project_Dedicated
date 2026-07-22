// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Framework/D1MatchTypes.h"
#include "Network/BackendTypes.h"
#include "D1MatchFlowComponent.generated.h"

class AController;
class AD1BomberGameState;
class AD1BomberPlayerState;

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
	void InitializeMatch(int32 InExpectedPlayers, float InWaitTimeoutSec, float InShutdownGraceSec,
		const FString& InMatchId, const FString& InMatchToken);

	/** 서버 전용: GameMode::PostLogin이 호출. 예상 인원 도달 시 매치 시작. */
	void HandlePlayerJoined();

private:
	void StartMatch();
	void OnWaitForPlayersTimeout();

	FTimerHandle WaitForPlayersTimerHandle;

	/** GameMode가 InitializeMatch로 주입. 시작 정원(0/1=즉시)과 게이트 타임아웃. */
	int32 ExpectedPlayerCount = 0;
	float WaitForPlayersTimeoutSec = 20.f;

//~ 사망·등수
public:
	/** 서버 전용: 사망 등록·등수 부여, 1명 남으면 매치 종료. PlayerState::ApplyHit이 GameState 경유로 호출. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	void EnsureAliveListInitialized();

	/** 생존 중인 비봇(실제) 플레이어 수. 봇전에서 실유저 전원 이탈/사망 시 즉시 종료 판정용. */
	int32 CountAliveRealPlayers() const;

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

//~ 매치 종료·셧다운
private:
	void OnMatchTimeExpired();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);

	FTimerHandle MatchTimerHandle;

	/** GameMode가 InitializeMatch로 주입. 셧다운 유예와 결과 POST 인증값. */
	float ShutdownGraceSec = 30.f;
	FString CurrentMatchId;
	FString CurrentMatchToken;

//~ 탈주 처리(게임중 kick·접속 끊김)
public:
	/** GameMode PreLogin 재입장 거절용 — 이미 kick된 유저인지. */
	bool IsUserKicked(int64 UserId) const { return KickedUserIds.Contains(UserId); }

	/** 서버 전용: GameMode::Logout이 호출. 매치 진행 중 이탈(끊김/나가기)을 탈주로 처리. */
	void NotifyPlayerDisconnected(AController* Exiting);

private:
	/** 백엔드 kick 대기열을 주기 폴링(DS·토큰 있을 때만). */
	void StartKickPolling();
	void StopKickPolling();
	void PollKicks();
	void HandleKickResponse(const FString& Body);
	/** 대상 유저를 kick — 온라인이면 탈주 처리·통지, 종료 후면 통지만. */
	void HandleKickUser(int64 UserId);
	/** 탈주 공용부(최하위·SetLeft·GameState 슬롯기록·결과 캡처·즉시정산·종료체크). bNotifyClient=false면 클라 통지 생략(끊김). */
	void ProcessLeaver(AD1BomberPlayerState* Target, bool bNotifyClient);

	FTimerHandle KickPollTimerHandle;

	/** 이미 kick 처리한 userId(중복 폴링·재입장 방어). */
	TSet<int64> KickedUserIds;

	/** 탈주자 결과 — Logout로 PlayerArray에서 빠지기 전에 캡처, EndMatch에서 병합(roster 인원 일치). */
	TArray<FMatchResultPlayer> LeftPlayers;
	TArray<FD1MatchResultEntry> LeftEntries;

//~ 상태 질의
private:
	/** GameState의 MatchPhase 단일 출처. */
	bool HasMatchStarted() const;
	bool IsMatchEnded() const;

	/** 소유 GameState. 없으면 nullptr. */
	AD1BomberGameState* GetBomberGameState() const;

	/** 서버 권위 여부. 모든 진입점 방어 가드. */
	bool HasServerAuthority() const;
};
