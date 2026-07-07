// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLobby.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Network/BackendErrorMessages.h"
#include "Network/D1AuthSubsystem.h"
#include "Network/D1MatchmakingSubsystem.h"

// 매치 정원(백엔드 playersPerMatch와 동일). match:found가 개수를 싣지 않아 클라 상수로 표기.
static constexpr int32 MatchPlayerCount = 4;

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
	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 서버 푸시(매칭 성사/큐 입장/에러) 구독
	if (UD1MatchmakingSubsystem* Matchmaking = GetGameInstance()->GetSubsystem<UD1MatchmakingSubsystem>())
	{
		Matchmaking->OnQueueJoined.AddDynamic(this, &UD1UWLobby::HandleQueueJoined);
		Matchmaking->OnMatchFound.AddDynamic(this, &UD1UWLobby::HandleMatchFound);
		Matchmaking->OnMatchmakingError.AddDynamic(this, &UD1UWLobby::HandleMatchmakingError);
	}

	if (UD1AuthSubsystem* Auth = GetGameInstance()->GetSubsystem<UD1AuthSubsystem>())
	{
		Auth->OnProfileUpdated.AddDynamic(this, &UD1UWLobby::HandleProfileUpdated);

		// 매치 후 ELO가 바뀌었을 수 있음 — 최신 프로필 재조회(완료 시 HandleProfileUpdated).
		Auth->RefreshMyProfile();
	}
}

void UD1UWLobby::NativeDestruct()
{
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
}

void UD1UWLobby::OnCancelMatchingClicked()
{
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
}

void UD1UWLobby::HandleMatchFound(const FMatchFoundDTO& Match)
{
	UE_LOG(LogD1, Log, TEXT("[Lobby] 매칭 완료 — server=%s:%d"),
		*Match.ServerHost, Match.ServerPort);

	if (MatchStatusLabel)
	{
		MatchStatusLabel->SetText(FText::Format(
			NSLOCTEXT("Lobby", "MatchFoundFmt", "매칭 완료! ({0}명) — 입장 중..."),
			FText::AsNumber(MatchPlayerCount)
		));
	}
	// 실제 DS 입장(ClientTravel)은 Matchmaking Subsystem이 처리. 위젯은 곧 travel로 소멸.
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

void UD1UWLobby::HandleProfileUpdated()
{
	// /api/auth/me 갱신 완료 — 최신 score/level로 라벨 새로고침.
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
}
