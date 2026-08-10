// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1BomberGameState.h"
#include "Network/D1SessionSubsystem.h"
#include "Systems/Map/D1MapCameraManager.h"
#include "TimerManager.h"
#include "UI/InGame/D1UWMatchResult.h"

AD1PlayerController::AD1PlayerController()
{
	PlayerCameraManagerClass = AD1MapCameraManager::StaticClass();
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

	TryBindMatchFinished();
}

void AD1PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
}

void AD1PlayerController::HandleMatchFinished()
{
	// GS는 이 함수를 부른 브로드캐스트의 발신자(또는 TryBind가 확보한 뒤 직접 호출) — null 불가.
	AD1BomberGameState* GS = GetWorld()->GetGameState<AD1BomberGameState>();
	if (!ResultClass)
	{
		// 무음이면 결과창 없이 UIOnly로 전환된 화면 정지가 원인 불명이 된다.
		UE_LOG(LogD1, Warning, TEXT("[MatchResult] ResultClass가 비어있음 (디테일 패널에서 지정 필요) — 결과창 미표시"));
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
		Result->SetResults(GS->GetFinalResults());
	}

	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());
}

void AD1PlayerController::TryBindMatchFinished()
{
	AD1BomberGameState* GS = GetWorld()->GetGameState<AD1BomberGameState>();
	if (!GS)
	{
		// GameState 복제 전 — 짧게 재시도.
		GetWorldTimerManager().SetTimer(
			BindRetryTimerHandle, this, &AD1PlayerController::TryBindMatchFinished, 0.2f, /*bLoop=*/false);
		return;
	}

	GetWorldTimerManager().ClearTimer(BindRetryTimerHandle);
	GS->OnMatchFinished.AddDynamic(this, &AD1PlayerController::HandleMatchFinished);

	// 이미 끝난 매치에 늦게 구독한 경우(재접속 등) 즉시 표시.
	if (GS->GetFinalResults().Num() > 0)
	{
		HandleMatchFinished();
	}
}

void AD1PlayerController::ClientNotifySessionSuperseded_Implementation()
{
	GetWorld()->GetGameInstance()->GetSubsystem<UD1SessionSubsystem>()->NotifySessionSuperseded();
}