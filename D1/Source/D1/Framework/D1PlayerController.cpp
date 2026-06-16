// Copyright Epic Games, Inc. All Rights Reserved.


#include "Framework/D1PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Framework/D1BomberGameState.h"
#include "Systems/Map/D1MapCameraManager.h"
#include "TimerManager.h"
#include "UI/InGame/D1UWMatchResult.h"
#include "Widgets/Input/SVirtualJoystick.h"

AD1PlayerController::AD1PlayerController()
{
	PlayerCameraManagerClass = AD1MapCameraManager::StaticClass();
}

void AD1PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 로컬 플레이어 컨트롤러에만 IMC 추가
	if (IsLocalPlayerController())
	{
		// 입력 매핑 컨텍스트 등록
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
}

void AD1PlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 로컬 컨트롤러에만 위젯 부착.
	// AddToPlayerScreen: 자기 LocalPlayer 영역에만 그려짐(멀티 LocalPlayer/Split Screen 안전).
	if (!IsLocalController())
	{
		return;
	}

	// 메뉴(D1MenuPlayerController)가 둔 UIOnly 입력 모드는 travel로 새 PC가 생겨도 안 풀린다.
	// 인게임에선 게임 입력으로 명시 복귀 — 안 하면 WASD/폭탄이 UI로 먹혀 조작 불가.
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	if (HUDClass && !HUDWidget)
	{
		HUDWidget = CreateWidget<UUserWidget>(this, HUDClass);
		if (HUDWidget)
		{
			HUDWidget->AddToPlayerScreen();
		}
	}

	// 매치 종료 시 결과 위젯을 띄우기 위해 GameState 이벤트 구독.
	TryBindMatchFinished();
}

void AD1PlayerController::HandleMatchFinished()
{
	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (!GS || !ResultClass)
	{
		return;
	}

	if (!ResultWidget)
	{
		ResultWidget = CreateWidget<UUserWidget>(this, ResultClass);
		if (ResultWidget)
		{
			ResultWidget->AddToPlayerScreen();
		}
	}

	if (UD1UWMatchResult* Result = Cast<UD1UWMatchResult>(ResultWidget))
	{
		Result->SetResults(GS->FinalResults);
	}

	// 결과 화면 — 마우스 커서 + UI 입력.
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());
}

void AD1PlayerController::TryBindMatchFinished()
{
	AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (!GS)
	{
		// GameState 복제 전 — 짧게 재시도.
		GetWorldTimerManager().SetTimer(
			BindRetryHandle, this, &AD1PlayerController::TryBindMatchFinished, 0.2f, /*bLoop=*/false);
		return;
	}

	GetWorldTimerManager().ClearTimer(BindRetryHandle);
	GS->OnMatchFinished.AddDynamic(this, &AD1PlayerController::HandleMatchFinished);

	// 이미 끝난 매치에 늦게 구독한 경우(재접속 등) 즉시 표시.
	if (GS->FinalResults.Num() > 0)
	{
		HandleMatchFinished();
	}
}