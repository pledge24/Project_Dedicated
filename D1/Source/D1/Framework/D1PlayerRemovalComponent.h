// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Framework/D1MatchTypes.h"
#include "Network/BackendTypes.h"
#include "D1PlayerRemovalComponent.generated.h"

class AController;
class AD1BomberGameState;
class AD1BomberPlayerState;
class UD1DsApiSubsystem;

/**
 *  게임중 플레이어 제거 담당 컴포넌트 (GameState 부착·서버 전용) — kick(다른 기기 로그인)과 탈주.
 *  kick 대기열 폴링·강제퇴장·탈주 확정(최하위 등수·SetLeft·즉시 정산·결과 캡처)을 소유.
 *  종료 판정은 소유하지 않는다 — 탈주 확정 후 MatchFlow::NotifyPlayerLeft로 심판에 넘긴다.
 */
UCLASS()
class UD1PlayerRemovalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UD1PlayerRemovalComponent();

//~ 킥·탈주
public:
	/** 서버 전용: GameMode::InitGameState가 설정만 주입. 폴링 가동은 StartKickPolling이 따로 한다. */
	void SetupForMatch(const FD1MatchSetupParams& Params);

	/**
	 * 서버 전용: 시작 게이트 통과 시 MatchFlow가 호출 — 끝내 입장하지 않은 유저를 재입장 거절 대상으로 확정.
	 * PlayerState가 생긴 적이 없어 RemoveLeaver를 태울 수 없다. 결과는 EndMatch의 AppendNoShowResults가 채운다.
	 */
	void NotifyNoShow(int64 UserId);

	/** 서버 전용: GameMode::Logout이 호출. 매치 진행 중 이탈(끊김/나가기)을 탈주로 처리. */
	void NotifyPlayerDisconnected(AController* Exiting);

	/** GameMode PreLogin 재입장 거절용 — 이미 kick된 유저인지. */
	bool IsUserKicked(int64 UserId) const { return KickedUserIds.Contains(UserId); }

	/**
	 * 서버 전용: GameMode::BeginPlay가 맵 빌드·봇 스폰 후 호출. 게임중 강제 회수(다른 기기 로그인)
	 * 대기열을 주기 폴링 시작 — 토큰 있는 실 DS에서만. HTTP는 D1DsApiSubsystem::FetchKicks 위임.
	 */
	void StartKickPolling();

	/** 서버 전용: 매치 종료 확정 시 MatchFlow가 호출 — 이후 kick은 재입장 거절·통지만 남는다. */
	void StopKickPolling();

	/** 정산 조립용 — kick·탈주 처리된 유저. PlayerArray쪽 중복 제외 기준. */
	const TSet<int64>& GetKickedUserIds() const { return KickedUserIds; }

	/** 탈주자 결과 캡처(Logout로 PlayerArray에서 빠지기 전) — EndMatch 병합용. */
	const TArray<FD1MatchResultEntry>& GetLeftEntries() const { return LeftEntries; }
	const TArray<FMatchResultPlayer>& GetLeftPlayers() const { return LeftPlayers; }

private:
	void PollKicks();
	/** 대상 유저를 kick — 온라인이면 탈주 처리·통지, 종료 후면 통지만. */
	void KickUser(int64 UserId);
	/** 탈주 공용부(최하위·SetLeft·GameState 슬롯기록·결과 캡처·즉시정산·심판 통지). bNotifyClient=false면 클라 통지 생략(끊김). */
	void RemoveLeaver(AD1BomberPlayerState* Target, bool bNotifyClient);

	FTimerHandle KickPollTimerHandle;

	/** GameMode가 SetupForMatch로 주입. 탈주 최하위 등수 산정과 kick 조회·즉시 정산 인증값. */
	int32 ExpectedPlayerCount = 0;
	FString CurrentMatchId;
	FString CurrentServerToken;

	/** 이미 kick 처리한 userId(중복 폴링·재입장 방어). */
	TSet<int64> KickedUserIds;

	/** 탈주자 결과 — Logout로 PlayerArray에서 빠지기 전에 캡처, EndMatch에서 병합(roster 인원 일치). */
	TArray<FMatchResultPlayer> LeftPlayers;
	TArray<FD1MatchResultEntry> LeftEntries;

	/** SetupForMatch 완료 여부 — 설정만 되고 가동 안 된 중간 상태를 StartKickPolling이 잡아낸다. */
	bool bIsSetupForMatch = false;

//~ 상태 질의
private:
	/** GameState의 MatchPhase 단일 출처. */
	bool HasMatchStarted() const;
	bool IsMatchEnded() const;

	/** 소유 GameState. 없으면 nullptr. */
	AD1BomberGameState* GetBomberGameState() const;

	/** kick 조회·즉시 정산용 DS API 클라이언트. 토큰 없으면(PIE/standalone) nullptr — 호출측은 스킵. */
	UD1DsApiSubsystem* GetDsApi() const;

	/** 서버 권위 여부. 모든 진입점 방어 가드. */
	bool HasServerAuthority() const;
};
