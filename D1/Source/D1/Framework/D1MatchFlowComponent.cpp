// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1MatchFlowComponent.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchTypes.h"
#include "Framework/D1PlayerController.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/BackendTypes.h"
#include "Network/D1BackendHttp.h"
#include "Network/D1DedicatedServerSubsystem.h"
#include "Network/D1MatchResultSubsystem.h"
#include "Network/D1OnlineSettings.h"
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

void UD1MatchFlowComponent::InitializeMatch(int32 InExpectedPlayers, float InWaitTimeoutSec, float InShutdownGraceSec,
	const FString& InMatchId, const FString& InMatchToken)
{
	if (!HasServerAuthority())
	{
		return;
	}

	ExpectedPlayerCount      = InExpectedPlayers;
	WaitForPlayersTimeoutSec = InWaitTimeoutSec;
	ShutdownGraceSec         = InShutdownGraceSec;
	CurrentMatchId           = InMatchId;
	CurrentMatchToken        = InMatchToken;

	// 게임중 강제 회수(다른 기기 로그인) 폴링 시작 — 토큰 있는 실 DS에서만.
	StartKickPolling();

	// 맵 빌드·시작 게이트 준비 완료 → 백엔드에 "플레이어 받을 준비됨" 통지(토큰 있는 실 DS만).
	// 백엔드는 이 콜백을 받고 클라에 match:found(입장 패킷) 전송. PIE/standalone은 토큰 없어 스킵.
	if (!CurrentMatchToken.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UD1MatchResultSubsystem* ResultClient = GI->GetSubsystem<UD1MatchResultSubsystem>())
				{
					ResultClient->ReportDSReady(CurrentMatchId, CurrentMatchToken);
				}
			}
		}
	}

	// 시작 게이트: 예상 인원 0/1(PIE·솔로)이면 즉시 시작, 아니면 전원 입장(PostLogin) 또는 타임아웃까지 Waiting.
	if (ExpectedPlayerCount <= 1)
	{
		StartMatch();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			WaitForPlayersTimerHandle, this, &UD1MatchFlowComponent::OnWaitForPlayersTimeout,
			WaitForPlayersTimeoutSec, /*bLoop=*/false);
		UE_LOG(LogD1, Log, TEXT("[Match] 시작 게이트 대기 — 예상 %d명 (타임아웃 %.0fs)"),
			ExpectedPlayerCount, WaitForPlayersTimeoutSec);
	}
}

void UD1MatchFlowComponent::HandlePlayerJoined()
{
	// 이미 시작했거나 게이트 비활성(PIE·솔로)이면 시작 게이트 카운트 생략.
	if (!HasServerAuthority() || HasMatchStarted() || ExpectedPlayerCount <= 1)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

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

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(WaitForPlayersTimerHandle);
	}

	if (AD1BomberGameState* GS = GetBomberGameState())
	{
		GS->MatchStartServerTime = GS->GetServerWorldTimeSeconds();
		GS->MatchPhase = EBomberMatchPhase::Playing;

		if (World)
		{
			World->GetTimerManager().SetTimer(
				MatchTimerHandle, this, &UD1MatchFlowComponent::OnMatchTimeExpired,
				GS->MatchDurationSec, /*bLoop=*/false);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 매치 시작 (Playing)"));
}

void UD1MatchFlowComponent::OnWaitForPlayersTimeout()
{
	if (HasMatchStarted())
	{
		return;
	}
	UE_LOG(LogD1, Warning, TEXT("[Match] 시작 게이트 타임아웃 — 현재 인원으로 시작"));
	StartMatch();
}

void UD1MatchFlowComponent::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	if (!HasServerAuthority() || IsMatchEnded() || !DeadPS)
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

void UD1MatchFlowComponent::EnsureAliveListInitialized()
{
	if (AlivePlayerStates.Num() > 0)
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

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
	if (bEndEvalPending)
	{
		return; // 프레임 내 다중 사망 → 타이머 1개만
	}
	if (UWorld* World = GetWorld())
	{
		bEndEvalPending = true;
		World->GetTimerManager().SetTimerForNextTick(
			this, &UD1MatchFlowComponent::EvaluateEndCondition);
	}
}

void UD1MatchFlowComponent::EvaluateEndCondition()
{
	bEndEvalPending = false;

	if (!HasServerAuthority() || IsMatchEnded())
	{
		PendingDeadBatch.Reset(); // 다른 경로가 이미 종료 → 배치 폐기
		return;
	}

	FlushPendingDeaths(); // 이번 프레임 배치에 공동 등수 부여

	if (AlivePlayerStates.Num() <= 1)
	{
		const bool bHasSurvivor = AlivePlayerStates.Num() == 1;
		AD1BomberPlayerState* Winner = bHasSurvivor ? AlivePlayerStates[0].Get() : nullptr;
		EndMatchWithWinner(Winner, bHasSurvivor ? EBomberEndReason::Winner : EBomberEndReason::Draw);
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

void UD1MatchFlowComponent::OnMatchTimeExpired()
{
	if (IsMatchEnded())
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// 생존자는 EndMatchWithWinner에서 공동 1위로 보정된다.
	EndMatchWithWinner(nullptr, EBomberEndReason::TimeExpired);
}

void UD1MatchFlowComponent::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason)
{
	if (IsMatchEnded())
	{
		return;
	}

	FlushPendingDeaths(); // 시간만료/탈주 등 다른 경로 종료 시에도 대기 사망자 등수 확정

	StopKickPolling();

	if (WinnerPS && WinnerPS->GetPlacement() <= 0)
	{
		WinnerPS->SetPlacement(1);
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (GS)
	{
		GS->MatchPhase = EBomberMatchPhase::Finished;
	}

	UE_LOG(LogD1, Log, TEXT("Match ended (%s). Winner=%s (Placement=%d)"),
		EndReasonToString(Reason),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->GetPlacement() : 0);

	UWorld* World = GetWorld();
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->DisableInput(PC);
			}
		}
	}

	if (!GS)
	{
		return;
	}

	// 최종 결과 스냅샷(UI 원자 복제) + 백엔드 보고용 수집을 한 번에.
	TArray<FD1MatchResultEntry> Entries;
	TArray<FMatchResultPlayer> ResultPlayers;
	Entries.Reserve(GS->PlayerArray.Num() + LeftEntries.Num());
	ResultPlayers.Reserve(GS->PlayerArray.Num() + LeftPlayers.Num());
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			// 탈주 유저는 이미 LeftPlayers/Entries로 캡처됨 — PlayerArray쪽 중복 방지.
			// (Logout 지연으로 아직 PlayerArray에 남아있을 수 있다.)
			if (KickedUserIds.Contains(B->GetBackendUserId()))
			{
				continue;
			}

			// 미배정 생존자(시간 만료/무승부)는 공동 1위로 보정 — 백엔드는 placement 1~N만 허용.
			if (B->GetPlacement() <= 0)
			{
				B->SetPlacement(1);
			}

			FD1MatchResultEntry Entry;
			Entry.Placement = B->GetPlacement();
			Entry.Nickname  = B->GetPlayerName();
			Entry.SlotIndex = B->GetPlayerSlotIndex();
			Entry.LivesLeft = B->GetLives();
			Entries.Add(Entry);

			FMatchResultPlayer RP;
			RP.UserId    = B->GetBackendUserId();
			RP.SlotIndex = B->GetPlayerSlotIndex();
			RP.Placement = B->GetPlacement();
			RP.LivesLeft = B->GetLives();
			ResultPlayers.Add(RP);
		}
	}

	// 탈주자 병합 — 결과 인원이 roster와 정확히 일치해야 백엔드 검증 통과.
	Entries.Append(LeftEntries);
	ResultPlayers.Append(LeftPlayers);

	// UI 표시용 결정적 순서: 등수 오름차순, 동률은 슬롯 순. (PlayerArray 순서는 비결정)
	Entries.Sort([](const FD1MatchResultEntry& A, const FD1MatchResultEntry& B)
	{
		return A.Placement != B.Placement ? A.Placement < B.Placement : A.SlotIndex < B.SlotIndex;
	});

	GS->SetFinalResults(Entries);

	// 백엔드가 띄운 DS일 때만 결과 보고(토큰 없으면 PIE/standalone → 스킵).
	if (!CurrentMatchToken.IsEmpty() && World)
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UD1MatchResultSubsystem* ResultClient = GI->GetSubsystem<UD1MatchResultSubsystem>())
			{
				const int32 DurationSec = FMath::Max(0,
					FMath::RoundToInt(GS->GetServerWorldTimeSeconds() - GS->MatchStartServerTime));
				ResultClient->ReportMatchResult(CurrentMatchId, CurrentMatchToken, GS->MapName,
					DurationSec, EndReasonToString(Reason), ResultPlayers);
			}
		}
	}

	// 클라들이 결과 화면 카운트다운 후 ClientTravel로 빠지면 DS가 스스로 종료.
	if (World)
	{
		if (UD1DedicatedServerSubsystem* DS = World->GetSubsystem<UD1DedicatedServerSubsystem>())
		{
			DS->BeginShutdownWatch(ShutdownGraceSec);
		}
	}
}

void UD1MatchFlowComponent::NotifyPlayerDisconnected(AController* Exiting)
{
	if (!HasServerAuthority() || !Exiting)
	{
		return;
	}

	AD1BomberPlayerState* PS = Exiting->GetPlayerState<AD1BomberPlayerState>();
	if (!PS)
	{
		return;
	}

	const int64 UserId = PS->GetBackendUserId();

	// 봇·이미 탈주·이미 kick 처리·매치 미시작/종료는 제외 — 진행 중 실유저 이탈만 탈주로.
	if (PS->IsBot() || PS->HasLeft() || UserId <= 0 || KickedUserIds.Contains(UserId)
		|| !HasMatchStarted() || IsMatchEnded())
	{
		return;
	}

	KickedUserIds.Add(UserId); // 재입장 거절 + 중복 방지
	ProcessLeaver(PS, /*bNotifyClient=*/false);
}

void UD1MatchFlowComponent::StartKickPolling()
{
	// 백엔드가 띄운 DS(토큰 보유)에서만 — PIE/standalone은 폴링 없음.
	if (CurrentMatchToken.IsEmpty())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(1.f, GetDefault<UD1OnlineSettings>()->KickPollIntervalSec);
		World->GetTimerManager().SetTimer(
			KickPollTimerHandle, this, &UD1MatchFlowComponent::PollKicks, Interval, /*bLoop=*/true);
	}
}

void UD1MatchFlowComponent::StopKickPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(KickPollTimerHandle);
	}
}

void UD1MatchFlowComponent::PollKicks()
{
	if (CurrentMatchId.IsEmpty() || CurrentMatchToken.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;

	const FString Path = FString::Printf(TEXT("/api/match/%s/kicks"), *CurrentMatchId);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(GI, Path, /*bAttachAuth=*/false);
	// 매치별 서버 토큰을 Bearer로 — 유저 JWT 아님(결과 POST와 동일 인증 채널).
	Request->SetHeader(TEXT("Authorization"), D1BackendHttp::MakeBearer(CurrentMatchToken));

	TWeakObjectPtr<UD1MatchFlowComponent> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			UD1MatchFlowComponent* Self = WeakThis.Get();
			if (Self && bSucceeded && Res.IsValid() && Res->GetResponseCode() == 200)
			{
				Self->HandleKickResponse(Res->GetContentAsString());
			}
		});
	Request->ProcessRequest();
}

void UD1MatchFlowComponent::HandleKickResponse(const FString& Body)
{
	TSharedPtr<FJsonObject> Root;
	if (!D1BackendHttp::DeserializeJson(Body, Root))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (!D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* UserIds = nullptr;
	if (!(*DataObj)->TryGetArrayField(TEXT("userIds"), UserIds))
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Value : *UserIds)
	{
		if (Value.IsValid())
		{
			HandleKickUser(static_cast<int64>(Value->AsNumber()));
		}
	}
}

void UD1MatchFlowComponent::HandleKickUser(int64 UserId)
{
	if (!HasServerAuthority() || UserId <= 0 || KickedUserIds.Contains(UserId))
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

	AD1BomberPlayerState* Target = nullptr;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			if (B->GetBackendUserId() == UserId)
			{
				Target = B;
				break;
			}
		}
	}

	// 표시(중복·재입장 방어). 미접속 유저면 접속 시 PreLogin이 거절.
	KickedUserIds.Add(UserId);

	if (!Target)
	{
		return;
	}

	// 매치가 이미 끝났으면 결과는 확정 — 통지만(로그인 복귀).
	if (IsMatchEnded())
	{
		if (AD1PlayerController* PC = Cast<AD1PlayerController>(Target->GetOwningController()))
		{
			PC->ClientNotifySessionSuperseded();
		}
		return;
	}

	ProcessLeaver(Target, /*bNotifyClient=*/true);
}

void UD1MatchFlowComponent::ProcessLeaver(AD1BomberPlayerState* Target, bool bNotifyClient)
{
	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS || !Target)
	{
		return;
	}

	const int64 UserId = Target->GetBackendUserId();

	// 탈주 처리(사망과 별개). 전원 꼴등(정원 고정), 캐릭터 사라짐, 결과 캡처(Logout로 빠지기 전).
	EnsureAliveListInitialized();
	const int32 LastPlacement = FMath::Max(ExpectedPlayerCount, GS->PlayerArray.Num());
	Target->SetPlacement(LastPlacement);
	Target->SetLeft(); // bLeft 복제 → 캐릭터 사라짐 + 카드 "탈주"

	// PS가 제거돼도(끊김/kick 후 disconnect) 카드가 "탈주"를 매치 끝까지 유지하도록 슬롯을 GameState에 복제 기록.
	GS->MarkSlotLeft(Target->GetPlayerSlotIndex(), Target->GetPlayerName());

	AlivePlayerStates.Remove(Target);

	FMatchResultPlayer RP;
	RP.UserId    = UserId;
	RP.SlotIndex = Target->GetPlayerSlotIndex();
	RP.Placement = LastPlacement;
	RP.LivesLeft = Target->GetLives();
	RP.Left      = true;
	LeftPlayers.Add(RP);

	FD1MatchResultEntry Entry;
	Entry.Placement = LastPlacement;
	Entry.Nickname  = Target->GetPlayerName();
	Entry.SlotIndex = Target->GetPlayerSlotIndex();
	Entry.LivesLeft = Target->GetLives();
	LeftEntries.Add(Entry);

	// 탈주 즉시 정산 — 백엔드가 최하위 확정값을 바로 반영(로비 즉시 반영). 토큰 있는 실 DS만.
	if (!CurrentMatchToken.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UD1MatchResultSubsystem* ResultClient = GI->GetSubsystem<UD1MatchResultSubsystem>())
				{
					ResultClient->ReportLeaver(CurrentMatchId, CurrentMatchToken, UserId);
				}
			}
		}
	}

	// 클라 통지(팝업 + 로그인 복귀) + 남은 시간 입력 차단. 끊김(disconnect)은 이미 떠나 생략.
	if (bNotifyClient)
	{
		if (AD1PlayerController* PC = Cast<AD1PlayerController>(Target->GetOwningController()))
		{
			PC->ClientNotifySessionSuperseded();
			PC->DisableInput(PC);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 탈주 처리 userId=%lld placement=%d"), UserId, LastPlacement);

	// 종료 판정은 다음 틱으로 미룬다(사망 파이프라인과 동일). 한 배치(kick 폴링 응답)·한 프레임의
	// 탈주자를 모두 캡처한 뒤 EvaluateEndCondition이 한 번만 판정 → 앞 탈주가 매치를 끝내
	// 뒤 탈주가 IsMatchEnded 가드에 스킵되던 순서 의존 제거. 전원 탈주는 생존 0 → Draw로 정확 판정.
	RequestEndEvaluation();
}

bool UD1MatchFlowComponent::HasMatchStarted() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->MatchPhase != EBomberMatchPhase::Waiting;
}

bool UD1MatchFlowComponent::IsMatchEnded() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->MatchPhase == EBomberMatchPhase::Finished;
}

AD1BomberGameState* UD1MatchFlowComponent::GetBomberGameState() const
{
	return Cast<AD1BomberGameState>(GetOwner());
}

bool UD1MatchFlowComponent::HasServerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
