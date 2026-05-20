// Copyright Epic Games, Inc. All Rights Reserved.


#include "D1PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "D1.h"
#include "D1MapCameraManager.h"
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