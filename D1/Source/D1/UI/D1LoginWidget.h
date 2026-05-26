// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1AuthWidgetBase.h"
#include "Online/BackendTypes.h"
#include "D1LoginWidget.generated.h"

class UButton;
class UEditableTextBox;

/**
 *  로그인 위젯.
 *  로그인 성공 시 GameInstance에 세션이 자동 저장되고, 이 위젯이 LobbyMap으로 OpenLevel.
 */
UCLASS()
class UD1LoginWidget : public UD1AuthWidgetBase
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

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_LoginId;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_Password;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Login;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_GotoRegister;

	/** WBP_Register 클래스 — 디테일 패널에서 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> RegisterWidgetClass;

	/** 로그인 성공 후 이동할 맵 — 디테일 패널에서 MP_Lobby 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> LobbyMap;
};
