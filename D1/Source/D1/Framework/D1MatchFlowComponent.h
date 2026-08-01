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
	void InitializeMatch(int32 InExpectedPlayers, float InWaitTimeoutSec, float InShutdownGraceSec,
		const FString& InMatchId, const FString& InMatchToken, const TArray<FD1JoinEntry>& InExpectedRoster);

	/** 서버 전용: GameMode::PostLogin이 호출. 예상 인원 도달 시 매치 시작. */
	void HandlePlayerJoined();

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
	bool bEndEvalPending = false;

//~ 매치 종료·셧다운
private:
	void OnMatchTimeExpired();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);
	/**
	 * 한 번도 입장하지 않은 roster 인원을 최하위 미입장자로 결과에 채운다.
	 * 이들은 PlayerState가 생긴 적이 없어 PlayerArray에도 LeftPlayers에도 없다 — 보정하지 않으면
	 * 백엔드 roster와 인원이 어긋나 정상 플레이한 나머지 인원의 결과까지 통째로 거부된다.
	 */
	void AppendNoShowResults(TArray<FD1MatchResultEntry>& InOutEntries, TArray<FMatchResultPlayer>& InOutPlayers) const;
	/**
	 * 결과 스냅샷(UI)과 백엔드 보고 페이로드에 한 명분을 동시 추가.
	 * 두 배열의 인원이 어긋나면 백엔드가 매치 전체 결과를 거부하므로 추가는 반드시 이 함수로.
	 */
	static void AppendResultPair(TArray<FD1MatchResultEntry>& InOutEntries, TArray<FMatchResultPlayer>& InOutPlayers,
		int64 UserId, int32 SlotIndex, int32 Placement, int32 LivesLeft, const FString& Nickname, bool bLeft);
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

//~ 탈주 처리(게임중 kick·접속 끊김)
public:
	/** GameMode PreLogin 재입장 거절용 — 이미 kick된 유저인지. */
	bool IsUserKicked(int64 UserId) const { return KickedUserIds.Contains(UserId); }

	/** 서버 전용: GameMode::Logout이 호출. 매치 진행 중 이탈(끊김/나가기)을 탈주로 처리. */
	void NotifyPlayerDisconnected(AController* Exiting);

private:
	/** 백엔드 kick 대기열을 주기 폴링(DS·토큰 있을 때만). HTTP는 MatchResultSubsystem::FetchKicks 위임. */
	void StartKickPolling();
	void StopKickPolling();
	void PollKicks();
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

	/** 결과 보고 Subsystem. 토큰 없으면(PIE/standalone) nullptr — 호출측은 보고 스킵. */
	UD1MatchResultSubsystem* GetResultClient() const;

	/** 서버 권위 여부. 모든 진입점 방어 가드. */
	bool HasServerAuthority() const;
};
