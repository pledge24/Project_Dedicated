// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLobby.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1GameInstance.h"
#include "Network/BackendErrorMessages.h"
#include "Network/D1AuthSubsystem.h"
#include "Network/D1MatchmakingSubsystem.h"
#include "Network/D1OnlineSettings.h"
#include "Network/D1SessionSubsystem.h"
#include "TimerManager.h"
#include "UI/D1UILayers.h"

// 매치 정원(백엔드 playersPerMatch와 동일). match:found가 개수를 싣지 않아 클라 상수로 표기.
static constexpr int32 MatchPlayerCount = 4;

void UD1UWLobby::NativeConstruct()
{
	Super::NativeConstruct();

	UD1GameInstance* GI = GetGameInstance<UD1GameInstance>();

	if (!GI || !GI->IsLoggedIn())
	{
		UE_LOG(LogD1, Warning, TEXT("[Lobby] 비로그인 상태로 진입 — Frontend로 복귀"));
		if (UD1SessionSubsystem* Session = GI ? GI->GetSubsystem<UD1SessionSubsystem>() : nullptr)
		{
			Session->TravelToFrontend();
		}
		return;
	}

	// 우선 캐시값으로 라벨 표시 → 아래 RefreshMyProfile 완료 시 최신값으로 교체.
	ApplyProfileToLabels();

	if (StartMatchingButton)
	{
		StartMatchingButton->OnClicked.AddDynamic(this, &UD1UWLobby::OnStartMatchingClicked);
	}
	if (CancelMatchingButton)
	{
		CancelMatchingButton->OnClicked.AddDynamic(this, &UD1UWLobby::OnCancelMatchingClicked);
	}
	if (RankingButton)
	{
		RankingButton->OnClicked.AddDynamic(this, &UD1UWLobby::OnRankingClicked);
	}
	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UD1MatchmakingSubsystem* Matchmaking = GetGameInstance()->GetSubsystem<UD1MatchmakingSubsystem>())
	{
		Matchmaking->OnQueueJoined.AddDynamic(this, &UD1UWLobby::HandleQueueJoined);
		Matchmaking->OnMatchFound.AddDynamic(this, &UD1UWLobby::HandleMatchFound);
		Matchmaking->OnMatchmakingError.AddDynamic(this, &UD1UWLobby::HandleMatchmakingError);

		// 구독 뒤에 확인 — 진행 중 매치가 있으면 응답이 OnMatchFound를 태우고 그대로 DS로 들어간다.
		// (끊긴 채 로비로 돌아온 클라의 유일한 복구 경로. 없으면 조용히 아무 일도 안 일어난다.)
		Matchmaking->CheckRejoinableMatch();
	}

	if (UD1AuthSubsystem* Auth = GetGameInstance()->GetSubsystem<UD1AuthSubsystem>())
	{
		Auth->OnProfileUpdated.AddDynamic(this, &UD1UWLobby::HandleProfileUpdated);

		// 매치 후 ELO가 바뀌었을 수 있음 — 최신 프로필 재조회(완료 시 HandleProfileUpdated).
		Auth->RefreshMyProfile();
	}

	// 로비 상주 동안 세션 유효성 주기 확인 — 다른 기기 로그인 감지(대체 시 로그인 화면 복귀).
	if (UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(5.f, GetDefault<UD1OnlineSettings>()->HeartbeatIntervalSec);
		World->GetTimerManager().SetTimer(
			SessionHeartbeatTimerHandle, this, &UD1UWLobby::SendSessionHeartbeat, Interval, /*bLoop=*/true);
	}
}

void UD1UWLobby::NativeDestruct()
{
	StopMatchSearchingElapsed();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SessionHeartbeatTimerHandle);
	}

	// 위젯이 Subsystem보다 먼저 소멸 — 구독 해제로 dangling 방지
	if (UGameInstance* GameInst = GetGameInstance())
	{
		if (UD1MatchmakingSubsystem* Matchmaking = GameInst->GetSubsystem<UD1MatchmakingSubsystem>())
		{
			Matchmaking->OnQueueJoined.RemoveDynamic(this, &UD1UWLobby::HandleQueueJoined);
			Matchmaking->OnMatchFound.RemoveDynamic(this, &UD1UWLobby::HandleMatchFound);
			Matchmaking->OnMatchmakingError.RemoveDynamic(this, &UD1UWLobby::HandleMatchmakingError);
		}
		if (UD1AuthSubsystem* Auth = GameInst->GetSubsystem<UD1AuthSubsystem>())
		{
			Auth->OnProfileUpdated.RemoveDynamic(this, &UD1UWLobby::HandleProfileUpdated);
		}
	}

	Super::NativeDestruct();
}

void UD1UWLobby::HandleProfileUpdated()
{
	ApplyProfileToLabels();
}

void UD1UWLobby::ApplyProfileToLabels()
{
	const UD1GameInstance* GI = GetGameInstance<UD1GameInstance>();
	if (!GI)
	{
		return;
	}
	const FAuthUserDTO& User = GI->GetCurrentUser();

	if (NicknameLabel)
	{
		NicknameLabel->SetText(FText::FromString(User.Nickname));
	}
	if (LevelLabel)
	{
		LevelLabel->SetText(FText::Format(
			NSLOCTEXT("Lobby", "LvFmt", "Lv. {0}"),
			FText::AsNumber(User.Level)
		));
	}
	if (ScoreLabel)
	{
		ScoreLabel->SetText(FText::AsNumber(User.Score));
	}

	// EXP 바 = 레벨 내 진행분. 백엔드와 동일하게 레벨당 1000, level은 서버 산출값을 신뢰(Clamp는 방어).
	const int32 ExpPerLevel = 1000;
	const int32 ExpInLevel = FMath::Clamp(User.Exp - (User.Level - 1) * ExpPerLevel, 0, ExpPerLevel);
	if (ExpBar)
	{
		ExpBar->SetPercent(static_cast<float>(ExpInLevel) / ExpPerLevel);
	}
	if (ExpLabel)
	{
		ExpLabel->SetText(FText::Format(
			NSLOCTEXT("Lobby", "ExpFmt", "{0} / {1} EXP"),
			FText::AsNumber(ExpInLevel),
			FText::AsNumber(ExpPerLevel)
		));
	}
}

void UD1UWLobby::OnStartMatchingClicked()
{
	UD1MatchmakingSubsystem* Matchmaking = GetGameInstance()->GetSubsystem<UD1MatchmakingSubsystem>();
	if (!Matchmaking)
	{
		return;
	}

	Matchmaking->StartMatchmaking();

	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(NSLOCTEXT("Lobby", "MatchConnecting", "매칭 준비 중..."));
	}
	if (StartMatchingButton)
	{
		StartMatchingButton->SetIsEnabled(false);
	}

	// 준비 중엔 0:00 정지 표시. 실제 카운트는 큐 입장(HandleQueueJoined)부터.
	MatchSearchingElapsedSec = 0;
	if (MatchSearchingElapsedLabel)
	{
		MatchSearchingElapsedLabel->SetText(FText::FromString(TEXT("0:00")));
	}
}

void UD1UWLobby::OnCancelMatchingClicked()
{
	StopMatchSearchingElapsed();

	if (UD1MatchmakingSubsystem* Matchmaking = GetGameInstance()->GetSubsystem<UD1MatchmakingSubsystem>())
	{
		Matchmaking->CancelMatchmaking();
	}

	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (StartMatchingButton)
	{
		StartMatchingButton->SetIsEnabled(true);
	}
}

void UD1UWLobby::HandleQueueJoined()
{
	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(NSLOCTEXT("Lobby", "MatchSearching", "상대를 찾는 중..."));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MatchSearchingElapsedTimerHandle, this,
			&UD1UWLobby::UpdateMatchSearchingElapsed, 1.f, /*bLoop=*/true);
	}
}

void UD1UWLobby::HandleMatchFound(const FMatchFoundDTO& Match)
{
	UE_LOG(LogD1, Log, TEXT("[Lobby] 매칭 완료 — server=%s:%d"),
		*Match.ServerHost, Match.ServerPort);

	StopMatchSearchingElapsed();

	// 재입장 경로(CheckRejoinableMatch)는 버튼 클릭 없이 도착 — 패널·버튼 상태를 직접 맞춘다.
	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (StartMatchingButton)
	{
		StartMatchingButton->SetIsEnabled(false);
	}

	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(FText::Format(
			NSLOCTEXT("Lobby", "MatchFoundFmt", "매칭 완료! ({0}명) — 입장 중..."),
			FText::AsNumber(MatchPlayerCount)
		));
	}
	// 매칭 완료 → 타이머·취소 버튼만 숨기고 상태 라벨('입장 중')은 남긴다.
	// 실제 DS 입장(ClientTravel)은 Matchmaking Subsystem이 처리.
	if (MatchSearchingElapsedLabel)
	{
		MatchSearchingElapsedLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (CancelMatchingButton)
	{
		CancelMatchingButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UD1UWLobby::HandleMatchmakingError(const FBackendResponse& Error)
{
	const FString Msg = FBackendErrorMessages::Resolve(Error);

	UE_LOG(LogD1, Warning, TEXT("[Lobby] 매칭 에러: %s"), *Msg);

	StopMatchSearchingElapsed();

	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(FText::FromString(Msg));
	}
	if (StartMatchingButton)
	{
		// 에러 문구는 패널에 남겨두고 다시 시도 가능하게 Start 재활성
		StartMatchingButton->SetIsEnabled(true);
	}

	// HandleMatchFound가 접은 위젯 원복 — travel 실패 후 재검색 UI가 온전하도록.
	if (MatchSearchingElapsedLabel)
	{
		MatchSearchingElapsedLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (CancelMatchingButton)
	{
		CancelMatchingButton->SetVisibility(ESlateVisibility::Visible);
	}
}

void UD1UWLobby::UpdateMatchSearchingElapsed()
{
	++MatchSearchingElapsedSec;
	if (MatchSearchingElapsedLabel)
	{
		const int32 Minutes = MatchSearchingElapsedSec / 60;
		const int32 Seconds = MatchSearchingElapsedSec % 60;
		MatchSearchingElapsedLabel->SetText(FText::FromString(
			FString::Printf(TEXT("%d:%02d"), Minutes, Seconds)));
	}
}

void UD1UWLobby::StopMatchSearchingElapsed()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MatchSearchingElapsedTimerHandle);
	}
}

void UD1UWLobby::OnRankingClicked()
{
	if (!RankingWidgetClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Lobby] RankingWidgetClass가 비어있음 (디테일 패널에서 WBP_Ranking 지정 필요)"));
		return;
	}

	if (RankingWidget && RankingWidget->IsInViewport())
	{
		return;
	}

	// SwitchToWidget 경유 금지(로비 파괴됨) — 팝업층에 직접 띄운다.
	RankingWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), RankingWidgetClass);
	if (RankingWidget)
	{
		RankingWidget->AddToViewport(D1UILayer::Popup);
	}
}

void UD1UWLobby::SendSessionHeartbeat()
{
	if (UD1AuthSubsystem* Auth = GetGameInstance()->GetSubsystem<UD1AuthSubsystem>())
	{
		Auth->SendHeartbeat();
	}
}
