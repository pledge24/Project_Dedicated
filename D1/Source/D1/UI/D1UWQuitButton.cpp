// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWQuitButton.h"

#include "Components/Button.h"
#include "Kismet/KismetSystemLibrary.h"

void UD1UWQuitButton::NativeConstruct()
{
	Super::NativeConstruct();

	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UD1UWQuitButton::OnQuitClicked);
	}
}

void UD1UWQuitButton::OnQuitClicked()
{
	// 클라 앱 즉시 종료. 서버 통지 불필요(매칭 WS는 소켓 close로 큐 자동 제거).
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
