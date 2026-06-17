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
	//~ APlayerController
	virtual void SetupInputComponent() override;

	virtual void BeginPlay() override;

	/** GameState.OnMatchFinished 콜백 — 결과 위젯 생성·표시. */
	UFUNCTION()
	void HandleMatchFinished();

	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** 인게임 HUD 위젯 클래스. BP에서 WBP_BomberHUD 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> HUDClass;

	/** 매치 결과 위젯 클래스. BP에서 WBP_MatchResult 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> ResultClass;

private:
	/** GameState가 준비되면 OnMatchFinished에 바인드. 아직이면 짧게 재시도. */
	void TryBindMatchFinished();

	UPROPERTY()
	TObjectPtr<UUserWidget> HUDWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> ResultWidget;

	FTimerHandle BindRetryTimerHandle;
};
