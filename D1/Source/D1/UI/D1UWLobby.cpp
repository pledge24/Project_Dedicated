// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLobby.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "D1.h"
#include "Kismet/GameplayStatics.h"
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
}

void UD1UWLobby::OnStartMatchingClicked()
{
	// 실제 큐 진입 로직은 매칭 슬라이스(F1)에서 추가. 지금은 UI 토글만.
	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (StartMatchingButton)
	{
		StartMatchingButton->SetIsEnabled(false);
	}
}

void UD1UWLobby::OnCancelMatchingClicked()
{
	if (MatchStatusPanel)
	{
		MatchStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (StartMatchingButton)
	{
		StartMatchingButton->SetIsEnabled(true);
	}
}
