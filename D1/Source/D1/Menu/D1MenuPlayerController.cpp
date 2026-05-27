// Copyright Epic Games, Inc. All Rights Reserved.

#include "Menu/D1MenuPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "D1.h"
#include "Menu/D1MenuGameMode.h"

AD1MenuPlayerController::AD1MenuPlayerController()
{
	bShowMouseCursor = true;
}

void AD1MenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 위젯은 로컬 PC에만 생성. Listen Server의 서버 PC는 LocalPlayer가 없어 AddToViewport 실패.
	if (!IsLocalController())
	{
		return;
	}

	ShowBackground();
	ShowInitialWidgetFromGameMode();
	ApplyUiOnlyInputMode();
}

void AD1MenuPlayerController::SwitchToWidget(TSubclassOf<UUserWidget> NewWidgetClass)
{
	if (!NewWidgetClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Menu] SwitchToWidget: NewWidgetClass가 null"));
		return;
	}

	if (CurrentWidget)
	{
		CurrentWidget->RemoveFromParent();
		CurrentWidget = nullptr;
	}

	CurrentWidget = CreateWidget<UUserWidget>(this, NewWidgetClass);
	if (CurrentWidget)
	{
		CurrentWidget->AddToViewport();
	}

	ApplyUiOnlyInputMode();
}

void AD1MenuPlayerController::ShowInitialWidgetFromGameMode()
{
	AD1MenuGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AD1MenuGameMode>() : nullptr;
	if (!GM)
	{
		UE_LOG(LogD1, Warning, TEXT("[Menu] AD1MenuGameMode를 찾지 못함 (서버 컨텍스트 아님)"));
		return;
	}

	TSubclassOf<UUserWidget> InitialClass = GM->GetInitialWidgetClass();
	if (!InitialClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Menu] GameMode의 InitialWidgetClass가 비어있음"));
		return;
	}

	SwitchToWidget(InitialClass);
}

void AD1MenuPlayerController::ShowBackground()
{
	if (!BackgroundWidgetClass || BackgroundWidget)
	{
		return;
	}

	BackgroundWidget = CreateWidget<UUserWidget>(this, BackgroundWidgetClass);
	if (BackgroundWidget)
	{
		// ZOrder = -1: 어떤 콘텐츠 위젯보다도 항상 뒤에 그려짐
		BackgroundWidget->AddToViewport(-1);
	}
}

void AD1MenuPlayerController::ApplyUiOnlyInputMode()
{
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (CurrentWidget)
	{
		Mode.SetWidgetToFocus(CurrentWidget->TakeWidget());
	}
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}
