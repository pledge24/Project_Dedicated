// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1LobbyWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "D1.h"
#include "Kismet/GameplayStatics.h"
#include "Online/D1GameInstance.h"

void UD1LobbyWidget::NativeConstruct()
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

	if (TextBlock_Nickname)
	{
		TextBlock_Nickname->SetText(FText::FromString(User.Nickname));
	}
	if (TextBlock_Score)
	{
		TextBlock_Score->SetText(FText::AsNumber(User.Score));
	}
	if (Button_StartMatching)
	{
		// 다음 슬라이스에서 활성화
		Button_StartMatching->SetIsEnabled(false);
	}
}
