// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLobby.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "D1.h"
#include "Kismet/GameplayStatics.h"
#include "Online/BackendErrorMessages.h"
#include "Online/BackendSubsystem.h"
#include "Online/D1GameInstance.h"

void UD1UWLobby::NativeConstruct()
{
	Super::NativeConstruct();

	UD1GameInstance* GI = GetGameInstance<UD1GameInstance>();

	// 비로그인 진입 방어 — Frontend로 강제 복귀
	if (!GI || !GI->IsLoggedIn())
	{
		UE_LOG(LogD1, Warning, TEXT("[Lobby] 비로그인 상태로 진입 — Frontend로 복귀"));
		if (!FrontendMap.IsNull())
		{
			UGameplayStatics::OpenLevelBySoftObjectPtr(this, FrontendMap);
		}
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
	if (StartMatchingButton)
	{
		StartMatchingButton->OnClicked.AddDynamic(this, &UD1UWLobby::OnStartMatchingClicked);
	}
	if (CancelMatchingButton)
	{
		CancelMatchingButton->OnClicked.AddDynamic(this, &UD1UWLobby::OnCancelMatchingClicked);
	}
	if (MatchStatusPanel)
	{
		// 매칭 진입 전 — 매칭 상태 패널 숨김
		MatchStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 서버 푸시(매칭 성사/큐 입장/에러) 구독
	if (UBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<UBackendSubsystem>())
	{
		Backend->OnQueueJoined.AddDynamic(this, &UD1UWLobby::HandleQueueJoined);
		Backend->OnMatchFound.AddDynamic(this, &UD1UWLobby::HandleMatchFound);
		Backend->OnMatchmakingError.AddDynamic(this, &UD1UWLobby::HandleMatchmakingError);
	}
}

void UD1UWLobby::NativeDestruct()
{
	// 위젯이 Subsystem보다 먼저 소멸 — 구독 해제로 dangling 방지
	if (UGameInstance* GameInst = GetGameInstance())
	{
		if (UBackendSubsystem* Backend = GameInst->GetSubsystem<UBackendSubsystem>())
		{
			Backend->OnQueueJoined.RemoveDynamic(this, &UD1UWLobby::HandleQueueJoined);
			Backend->OnMatchFound.RemoveDynamic(this, &UD1UWLobby::HandleMatchFound);
			Backend->OnMatchmakingError.RemoveDynamic(this, &UD1UWLobby::HandleMatchmakingError);
		}
	}

	Super::NativeDestruct();
}

void UD1UWLobby::OnStartMatchingClicked()
{
	UBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<UBackendSubsystem>();
	if (!Backend)
	{
		return;
	}

	Backend->StartMatchmaking();

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
}

void UD1UWLobby::OnCancelMatchingClicked()
{
	if (UBackendSubsystem* Backend = GetGameInstance()->GetSubsystem<UBackendSubsystem>())
	{
		Backend->CancelMatchmaking();
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
}

void UD1UWLobby::HandleMatchFound(const FMatchFoundDTO& Match)
{
	UE_LOG(LogD1, Log, TEXT("[Lobby] 매칭 완료 — server=%s:%d players=%d"),
		*Match.ServerHost, Match.ServerPort, Match.Players.Num());

	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(FText::Format(
			NSLOCTEXT("Lobby", "MatchFoundFmt", "매칭 완료! ({0}명)"),
			FText::AsNumber(Match.Players.Num())
		));
	}
	// 실제 DS 입장(travel)은 F1c. 지금은 표시까지.
}

void UD1UWLobby::HandleMatchmakingError(const FBackendResponse& Error)
{
	const FString Msg = Error.ErrorMessage.IsEmpty()
		? FBackendErrorMessages::Lookup(Error.ErrorCode)
		: Error.ErrorMessage;

	UE_LOG(LogD1, Warning, TEXT("[Lobby] 매칭 에러: %s"), *Msg);

	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(FText::FromString(Msg));
	}
	if (StartMatchingButton)
	{
		// 에러 문구는 패널에 남겨두고 다시 시도 가능하게 Start 재활성
		StartMatchingButton->SetIsEnabled(true);
	}
}
