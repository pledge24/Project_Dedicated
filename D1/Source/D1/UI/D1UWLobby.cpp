// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLobby.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
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
	if (ScoreLabel)
	{
		ScoreLabel->SetText(FText::AsNumber(User.Score));
	}
	if (StartMatchingButton)
	{
		// 다음 슬라이스에서 활성화
		StartMatchingButton->SetIsEnabled(false);
	}
}
