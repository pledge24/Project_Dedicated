// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1MatchFlowComponent.h"

#include "Core/D1LogChannels.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchSettlement.h"
#include "Framework/D1MatchTypes.h"
#include "Framework/D1PlayerRemovalComponent.h"
#include "Game/Character/D1BomberCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Network/BackendTypes.h"
#include "Network/D1DsApiSubsystem.h"
#include "Network/D1DsShutdownSubsystem.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* EndReasonToString(EBomberEndReason Reason)
	{
		switch (Reason)
		{
		case EBomberEndReason::Winner:      return TEXT("winner");
		case EBomberEndReason::Draw:        return TEXT("draw");
		case EBomberEndReason::TimeExpired: return TEXT("time_expired");
		default:                            return TEXT("abort");
		}
	}
}

UD1MatchFlowComponent::UD1MatchFlowComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UD1MatchFlowComponent::SetupForMatch(const FD1MatchSetupParams& Params)
{
	if (!HasServerAuthority())
	{
		return;
	}

	ExpectedPlayerCount      = Params.ExpectedPlayerCount;
	WaitForPlayersTimeoutSec = Params.WaitForPlayersTimeoutSec;
	ShutdownGraceSec         = Params.ShutdownGraceSec;
	CurrentMatchId           = Params.MatchId;
	CurrentServerToken       = Params.ServerToken;
	ExpectedRoster           = Params.ExpectedRoster;
	bIsSetupForMatch         = true;
}

void UD1MatchFlowComponent::StartMatchGate()
{
	// 설정 없이 게이트만 열리면 정원 0으로 즉시 시작하고 토큰 없이 결과 보고를 스킵한다 — 무음 오작동.
	if (!ensureMsgf(bIsSetupForMatch, TEXT("[Match] 시작 게이트 가동 전 SetupForMatch 누락")))
	{
		return;
	}

	bIsMatchGateStarted = true;

	// 맵 빌드·봇 스폰 완료 → 백엔드에 "플레이어 받을 준비됨" 통지(토큰 있는 실 DS만).
	// 백엔드는 이 콜백을 받고 클라에 match:found(입장 패킷) 전송. PIE/standalone은 토큰 없어 스킵.
	if (UD1DsApiSubsystem* DsApi = GetDsApi())
	{
		DsApi->ReportDsReady(CurrentMatchId, CurrentServerToken);
	}

	// 시작 게이트: 예상 인원 0/1(PIE·솔로)이면 즉시 시작, 아니면 전원 입장(PostLogin) 또는 타임아웃까지 Waiting.
	if (ExpectedPlayerCount <= 1)
	{
		StartMatch();
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(
			WaitForPlayersTimerHandle, this, &UD1MatchFlowComponent::StartMatchOnGateTimeout,
			WaitForPlayersTimeoutSec, /*bLoop=*/false);
		UE_LOG(LogD1, Log, TEXT("[Match] 시작 게이트 대기 — 예상 %d명 (타임아웃 %.0fs)"),
			ExpectedPlayerCount, WaitForPlayersTimeoutSec);
	}
}

void UD1MatchFlowComponent::NotifyPlayerJoined()
{
	// 게이트를 열기 전(맵 빌드 실패로 셧다운 유예 중 입장 등), 이미 시작했거나
	// 게이트 비활성(PIE·솔로)이면 시작 게이트 카운트 생략.
	if (!HasServerAuthority() || !bIsMatchGateStarted || HasMatchStarted() || ExpectedPlayerCount <= 1)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();

	int32 Connected = 0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (Cast<AD1BomberPlayerState>(PS))
		{
			++Connected;
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 입장 %d/%d"), Connected, ExpectedPlayerCount);

	if (Connected >= ExpectedPlayerCount)
	{
		StartMatch();
	}
}

void UD1MatchFlowComponent::StartMatch()
{
	if (HasMatchStarted())
	{
		return;
	}

	// 여기서 못 멈추면 phase 미전이·매치 타이머 미장전인 채 아래 "매치 시작" 로그까지 진행된다.
	AD1BomberGameState* GS = GetBomberGameState();
	if (!ensureMsgf(GS, TEXT("[Match] StartMatch: GameState 없음 — 시작 불가")))
	{
		return;
	}

	UWorld* World = GetWorld();
	World->GetTimerManager().ClearTimer(WaitForPlayersTimerHandle);

	GS->SetMatchStartServerTime(GS->GetServerWorldTimeSeconds());
	GS->SetMatchPhase(EBomberMatchPhase::Playing);

	World->GetTimerManager().SetTimer(
		MatchTimerHandle, this, &UD1MatchFlowComponent::EndMatchByTimeout,
		GS->GetMatchDurationSec(), /*bLoop=*/false);

	// 시작 게이트 해제 — 입장 시 서버가 잠근 이동(Restart의 MOVE_None) 일괄 재개.
	for (AD1BomberCharacter* Character : TActorRange<AD1BomberCharacter>(World))
	{
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		if (Move && Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Walking);
		}
	}

	// 여기부터 MatchPhase가 Playing이라 이후 접속은 PreLogin이 거절한다 — 안 온 사람을 지금 확정한다.
	MarkNoShowUsers();

	// 백엔드의 재입장 주소 발급 중단(토큰 있는 실 DS만). 재입장 허용 창은 여기서 닫힌다.
	if (UD1DsApiSubsystem* DsApi = GetDsApi())
	{
		DsApi->ReportMatchStarted(CurrentMatchId, CurrentServerToken);
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 매치 시작 (Playing)"));
}

void UD1MatchFlowComponent::StartMatchOnGateTimeout()
{
	if (HasMatchStarted())
	{
		return;
	}
	UE_LOG(LogD1, Warning, TEXT("[Match] 시작 게이트 타임아웃 — 현재 인원으로 시작"));
	StartMatch();
}

void UD1MatchFlowComponent::MarkNoShowUsers()
{
	if (ExpectedRoster.Num() == 0)
	{
		return; // PIE/standalone은 명단이 없어 판정 기준 자체가 없다.
	}

	AD1BomberGameState* GS = GetBomberGameState();
	UD1PlayerRemovalComponent* Removal = GS->GetPlayerRemoval();

	TSet<int64> JoinedUserIds;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AD1BomberPlayerState* BPS = Cast<AD1BomberPlayerState>(PS))
		{
			JoinedUserIds.Add(BPS->GetBackendUserId());
		}
	}

	int32 NoShowCount = 0;
	for (const FD1JoinEntry& Expected : ExpectedRoster)
	{
		if (!JoinedUserIds.Contains(Expected.UserId))
		{
			Removal->NotifyNoShow(Expected.UserId);
			++NoShowCount;
			UE_LOG(LogD1, Warning, TEXT("[Match] 미입장 확정 userId=%lld — 이후 입장 거절"), Expected.UserId);
		}
	}

	if (NoShowCount > 0)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 미입장 %d명 두고 시작"), NoShowCount);
	}
}

void UD1MatchFlowComponent::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	// 게이트를 열기 전 사망은 매치가 성립하지 않은 것 — 정산·보고 경로에 태우지 않는다.
	if (!HasServerAuthority() || !bIsMatchGateStarted || IsMatchEnded() || !DeadPS)
	{
		return;
	}

	EnsureAliveListInitialized();

	// 기록만: 등수는 다음 틱 Flush에서 배치 단위로 부여(동시 사망 공동 등수).
	if (DeadPS->GetPlacement() <= 0 && !PendingDeadBatch.Contains(DeadPS))
	{
		AlivePlayerStates.Remove(DeadPS);
		PendingDeadBatch.Add(DeadPS);
		UE_LOG(LogD1, Log, TEXT("Player died (pending): %s Remaining=%d Batch=%d"),
			*DeadPS->GetPlayerName(), AlivePlayerStates.Num(), PendingDeadBatch.Num());
	}

	RequestEndEvaluation();
}

void UD1MatchFlowComponent::NotifyPlayerLeft(AD1BomberPlayerState* LeftPS)
{
	if (!HasServerAuthority() || !bIsMatchGateStarted || IsMatchEnded() || !LeftPS)
	{
		return;
	}

	// 탈주자는 이미 최하위 등수가 부여된 상태(Placement>0) — 목록 초기화 시점과 무관하게 생존에서 빠진다.
	EnsureAliveListInitialized();
	AlivePlayerStates.Remove(LeftPS);
	RequestEndEvaluation();
}

void UD1MatchFlowComponent::EnsureAliveListInitialized()
{
	if (AlivePlayerStates.Num() > 0)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* BPS = Cast<AD1BomberPlayerState>(PS))
		{
			// ApplyHit가 NotifyPlayerDied보다 먼저 bIsAlive를 꺼서, 첫 사망자가
			// 누락되면 등수가 1 모자람. 미랭크(Placement<=0) 기준으로 전원 포함.
			// 단, 이미 대기 배치에 든 사망자는 등수 부여 전(Placement<=0)이라도 재추가 금지.
			if (BPS->GetPlacement() <= 0 && !PendingDeadBatch.Contains(BPS))
			{
				AlivePlayerStates.Add(BPS);
			}
		}
	}
}

void UD1MatchFlowComponent::RequestEndEvaluation()
{
	if (bEndEvaluationPending)
	{
		return; // 프레임 내 다중 사망 → 타이머 1개만
	}
	bEndEvaluationPending = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		this, &UD1MatchFlowComponent::EvaluateEndCondition);
}

void UD1MatchFlowComponent::EvaluateEndCondition()
{
	bEndEvaluationPending = false;

	if (!HasServerAuthority() || IsMatchEnded())
	{
		PendingDeadBatch.Reset(); // 다른 경로가 이미 종료 → 배치 폐기
		return;
	}

	FlushPendingDeaths(); // 이번 프레임 배치에 공동 등수 부여

	// Logout으로 PS 액터가 파괴되면(봇·roster 미등록 이탈은 NotifyPlayerDisconnected가 걸러냄)
	// 무효 엔트리가 남아 생존자 수를 부풀린다 — 종료 판정 전 정리.
	AlivePlayerStates.RemoveAll([](const TObjectPtr<AD1BomberPlayerState>& PS)
	{
		return !IsValid(PS);
	});

	if (AlivePlayerStates.Num() <= 1)
	{
		const bool bHasSurvivor = AlivePlayerStates.Num() == 1;
		AD1BomberPlayerState* Winner = bHasSurvivor ? AlivePlayerStates[0].Get() : nullptr;
		EndMatch(Winner, bHasSurvivor ? EBomberEndReason::Winner : EBomberEndReason::Draw);
	}
}

void UD1MatchFlowComponent::FlushPendingDeaths()
{
	if (PendingDeadBatch.Num() == 0)
	{
		return;
	}

	// 동률 등수 = 배치 시작 시점 생존자 수 = (배치 후 생존자) + (배치 인원).
	// 단일 사망이면 배치=1 → 죽는 시점 생존자 수(자기 포함), 기존 순차 등수와 동일.
	// 동반 폭사(P2)면 전원이 이 값을 공유하고 상위 등수(예: 1위)는 공석.
	const int32 TiePlacement = AlivePlayerStates.Num() + PendingDeadBatch.Num();
	for (AD1BomberPlayerState* PS : PendingDeadBatch)
	{
		if (PS && PS->GetPlacement() <= 0)
		{
			PS->SetPlacement(TiePlacement);
		}
	}
	PendingDeadBatch.Reset();
}

void UD1MatchFlowComponent::EndMatchByTimeout()
{
	if (IsMatchEnded())
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// 생존자는 EndMatchWithWinner에서 공동 1위로 보정된다.
	EndMatch(nullptr, EBomberEndReason::TimeExpired);
}

void UD1MatchFlowComponent::EndMatch(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason)
{
	if (IsMatchEnded())
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	UD1PlayerRemovalComponent* Removal = GS ? GS->GetPlayerRemoval() : nullptr;
	// 서두 단일 게이트 — 중간에 멈추면 phase 전이 후 결과 스냅샷·보고·셧다운이 부분 유실된다.
	if (!ensureMsgf(GS && Removal, TEXT("[Match] EndMatch: GameState/RemovalComp 없음 — 정산 불가")))
	{
		return;
	}

	FlushPendingDeaths(); // 시간만료/탈주 등 다른 경로 종료 시에도 대기 사망자 등수 확정

	// 종료 확정 — 이후 kick은 재입장 거절·통지만 남도록 폴링 중지(킥·탈주 소유는 RemovalComp).
	Removal->StopKickPolling();

	if (WinnerPS && WinnerPS->GetPlacement() <= 0)
	{
		WinnerPS->SetPlacement(1);
	}

	GS->SetMatchPhase(EBomberMatchPhase::Finished);

	UE_LOG(LogD1, Log, TEXT("Match ended (%s). Winner=%s (Placement=%d)"),
		EndReasonToString(Reason),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->GetPlacement() : 0);

	UWorld* World = GetWorld();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->DisableInput(PC);
		}
	}

	// 미배정 생존자(시간 만료/무승부)는 공동 1위로 보정 — 백엔드는 placement 1~N만 허용.
	// 탈주·kick 처리자는 RemoveLeaver가 이미 최하위를 부여해 여기 걸리지 않는다.
	for (APlayerState* PS : GS->PlayerArray)
	{
		AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS);
		if (B && B->GetPlacement() <= 0)
		{
			B->SetPlacement(1);
		}
	}

	// 최종 결과 스냅샷(UI 원자 복제) + 백엔드 보고 페이로드 — 조립은 정산 헬퍼에 위임.
	// 탈주 캡처·kick 명단은 RemovalComp 소유 — 여기서 병합만 한다.
	TArray<FD1MatchResultEntry> Entries;
	TArray<FMatchResultPlayer> ResultPlayers;
	D1MatchSettlement::BuildFinalResults(*GS, Removal->GetKickedUserIds(),
		Removal->GetLeftEntries(), Removal->GetLeftPlayers(),
		ExpectedRoster, ExpectedPlayerCount, Entries, ResultPlayers);

	GS->SetFinalResults(Entries);

	// 백엔드가 띄운 DS일 때만 결과 보고(토큰 없으면 PIE/standalone → 스킵).
	UD1DsApiSubsystem* DsApi = GetDsApi();
	if (!DsApi || !World)
	{
		// 보고할 곳이 없으면 기다릴 이유도 없다(PIE/standalone).
		BeginShutdownAfterReport();

		return;
	}

	// 보고가 확정되기 전에 프로세스가 죽으면 인플라이트 요청이 통째로 사라진다 —
	// 셧다운 감시는 결과 POST가 확정(성공·409·확정 실패·재시도 소진)된 뒤에 시작한다.
	const int32 DurationSec = FMath::Max(0,
		FMath::RoundToInt(GS->GetServerWorldTimeSeconds() - GS->GetMatchStartServerTime()));
	DsApi->ReportMatchResult(CurrentMatchId, CurrentServerToken, GS->GetMapName(),
		DurationSec, EndReasonToString(Reason), ResultPlayers,
		FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			BeginShutdownAfterReport();
		}));

	// 안전망 — 보고가 어떤 이유로든 확정 콜백에 도달하지 못해도 DS가 영원히 살아있지는 않게 한다.
	World->GetTimerManager().SetTimer(ResultReportHardCapTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogD1, Warning, TEXT("[Match] 결과 보고 하드캡(%.0fs) 도달 — 보고 대기 포기하고 종료 진행"),
				ResultReportHardCapSec);
			BeginShutdownAfterReport();
		}),
		ResultReportHardCapSec, /*bLoop=*/false);
}

void UD1MatchFlowComponent::BeginShutdownAfterReport()
{
	UWorld* World = GetWorld();
	World->GetTimerManager().ClearTimer(ResultReportHardCapTimerHandle);

	// 클라들이 결과 화면 카운트다운 후 ClientTravel로 빠지면 DS가 스스로 종료.
	// 확정 콜백과 하드캡이 모두 도달할 수 있지만 BeginShutdownWatch가 멱등이라 첫 호출만 유효하다.
	World->GetSubsystem<UD1DsShutdownSubsystem>()->BeginShutdownWatch(ShutdownGraceSec);
}

bool UD1MatchFlowComponent::HasMatchStarted() const
{
	return GetBomberGameState()->GetMatchPhase() != EBomberMatchPhase::Waiting;
}

bool UD1MatchFlowComponent::IsMatchEnded() const
{
	return GetBomberGameState()->GetMatchPhase() == EBomberMatchPhase::Finished;
}

AD1BomberGameState* UD1MatchFlowComponent::GetBomberGameState() const
{
	return Cast<AD1BomberGameState>(GetOwner());
}

UD1DsApiSubsystem* UD1MatchFlowComponent::GetDsApi() const
{
	if (CurrentServerToken.IsEmpty())
	{
		return nullptr;
	}

	return GetWorld()->GetGameInstance()->GetSubsystem<UD1DsApiSubsystem>();
}

bool UD1MatchFlowComponent::HasServerAuthority() const
{
	return GetOwner()->HasAuthority();
}
