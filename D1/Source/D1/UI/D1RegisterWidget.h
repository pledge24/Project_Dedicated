// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1AuthWidgetBase.h"
#include "Online/BackendTypes.h"
#include "D1RegisterWidget.generated.h"

class UButton;
class UEditableTextBox;

/**
 *  회원가입 위젯.
 *  성공 시 자동으로 LoginWidget으로 돌아간다 (로그인 직접 진행 X — 사용자 의도 확인 위해).
 */
UCLASS()
class UD1RegisterWidget : public UD1AuthWidgetBase
{
	GENERATED_BODY()

protected:
	//~ UUserWidget
	virtual void NativeConstruct() override;

	UFUNCTION()
	void OnRegisterClicked();

	UFUNCTION()
	void OnBackToLoginClicked();

	UFUNCTION()
	void OnRegisterCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_LoginId;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_Password;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_Nickname;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Register;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_BackToLogin;

	/** WBP_Login 클래스 — 디테일 패널에서 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Register", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> LoginWidgetClass;
};
