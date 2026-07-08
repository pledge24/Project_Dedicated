// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UWAuthBase.h"
#include "Network/BackendTypes.h"
#include "D1UWRegister.generated.h"

class UButton;
class UEditableTextBox;

/**
 *  회원가입 위젯.
 *  성공 시 자동으로 LoginWidget으로 돌아간다 (로그인 직접 진행 X — 사용자 의도 확인 위해).
 */
UCLASS()
class UD1UWRegister : public UD1UWAuthBase
{
	GENERATED_BODY()

//~ 공통
protected:
	virtual void NativeConstruct() override;
	virtual UButton* GetSubmitButton() const override { return RegisterButton; }

//~ 회원가입 제출
protected:
	UFUNCTION()
	void OnRegisterClicked();

	UFUNCTION()
	void OnRegisterCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UEditableTextBox> NicknameTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> RegisterButton;

//~ 로그인 전환
protected:
	UFUNCTION()
	void OnBackToLoginClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> BackToLoginButton;

private:
	/** WBP_Login 클래스 — 디테일 패널에서 지정. 가입 성공 복귀에도 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Register", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UUserWidget> LoginWidgetClass;
};
