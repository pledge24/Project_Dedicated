// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/Menu/D1MenuPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Core/D1LogChannels.h"
#include "Framework/Menu/D1MenuGameMode.h"
#include "Widgets/SWidget.h"

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
	// 입력 모드는 SwitchToWidget 끝에서 적용됨 (여기서 중복 호출 안 함)
	ShowInitialWidgetFromGameMode();
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

void AD1MenuPlayerController::ApplyUiOnlyInputMode()
{
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (CurrentWidget)
	{
		// 루트 UserWidget은 기본 비-포커스 → 첫 포커스 가능 자식을 대상으로 (UIOnly 포커스 에러 회피)
		if (UWidget* FocusTarget = FindFirstFocusableWidget(CurrentWidget))
		{
			Mode.SetWidgetToFocus(FocusTarget->TakeWidget());
		}
	}
	SetInputMode(Mode);
	SetShowMouseCursor(true);
}

UWidget* AD1MenuPlayerController::FindFirstFocusableWidget(UUserWidget* Root)
{
	if (!Root || !Root->WidgetTree)
	{
		return nullptr;
	}

	// ForEachWidgetUntil은 UMG_API 미익스포트라 모듈 외부에서 링크 불가 →
	// 익스포트된 ForEachWidgetAndDescendants + Found 가드로 첫 매치만 취함
	UWidget* Found = nullptr;
	Root->WidgetTree->ForEachWidgetAndDescendants([&Found](UWidget* Widget)
	{
		if (Found)
		{
			return;
		}

		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		if (Slate.IsValid() && Slate->SupportsKeyboardFocus())
		{
			Found = Widget;
		}
	});
	return Found;
}
