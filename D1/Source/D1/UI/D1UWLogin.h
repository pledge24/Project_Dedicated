// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UWAuthBase.h"
#include "Network/BackendTypes.h"
#include "D1UWLogin.generated.h"

class UButton;
class UEditableTextBox;

/**
 *  로그인 위젯.
 *  로그인 성공 시 GameInstance에 세션이 자동 저장되고, 이 위젯이 LobbyMap으로 OpenLevel.
 */
UCLASS()
class UD1UWLogin : public UD1UWAuthBase
{
	GENERATED_BODY()

protected:
	//~ UUserWidget
	virtual void NativeConstruct() override;

	UFUNCTION()
	void OnLoginClicked();

	UFUNCTION()
	void OnGotoRegisterClicked();

	UFUNCTION()
	void OnLoginCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UEditableTextBox> LoginIdTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> GotoRegisterButton;

private:
	/** WBP_Register 클래스 — 디테일 패널에서 지정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Login", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> RegisterWidgetClass;

	/** 로그인 성공 후 이동할 맵 — 디테일 패널에서 MP_Lobby 지정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Login", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> LobbyMap;
};
