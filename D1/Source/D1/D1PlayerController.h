// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "D1PlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  봄버맨 PlayerController.
 *  카메라 선택은 AD1MapCameraManager에 위임.
 */
UCLASS(abstract)
class AD1PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AD1PlayerController();

protected:

	/** 입력 매핑 컨텍스트 */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** 입력 매핑 컨텍스트 */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** 입력 매핑 컨텍스트 설정 */
	virtual void SetupInputComponent() override;
};
