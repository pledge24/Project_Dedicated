// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWRegister.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "D1.h"
#include "Menu/D1MenuPlayerController.h"
#include "Online/BackendErrorMessages.h"
#include "Online/BackendSubsystem.h"

void UD1UWRegister::NativeConstruct()
{
	Super::NativeConstruct();

	if (RegisterButton)
	{
		RegisterButton->OnClicked.AddDynamic(this, &UD1UWRegister::OnRegisterClicked);
	}
	if (BackToLoginButton)
	{
		BackToLoginButton->OnClicked.AddDynamic(this, &UD1UWRegister::OnBackToLoginClicked);
	}
}

void UD1UWRegister::OnRegisterClicked()
{
	UBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		UE_LOG(LogD1, Error, TEXT("[Register] UBackendSubsystem를 찾을 수 없음"));
		EndRequest(false, FBackendErrorMessages::Lookup(EBackendErrorCode::InternalError));
		return;
	}

	const FString LoginId = LoginIdTextBox ? LoginIdTextBox->GetText().ToString() : FString();
	const FString Password = PasswordTextBox ? PasswordTextBox->GetText().ToString() : FString();
	const FString Nickname = NicknameTextBox ? NicknameTextBox->GetText().ToString() : FString();

	BeginRequest();
	if (RegisterButton)
	{
		RegisterButton->SetIsEnabled(false);
	}

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1UWRegister::OnRegisterCompletedInternal);
	Backend->Register(LoginId, Password, Nickname, Cb);
}

void UD1UWRegister::OnBackToLoginClicked()
{
	if (!LoginWidgetClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Register] LoginWidgetClass가 비어있음"));
		return;
	}

	AD1MenuPlayerController* PC = Cast<AD1MenuPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->SwitchToWidget(LoginWidgetClass);
	}
}

void UD1UWRegister::OnRegisterCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (RegisterButton)
	{
		RegisterButton->SetIsEnabled(true);
	}

	if (!Response.bOk)
	{
		const FString Msg = Response.ErrorMessage.IsEmpty()
			? FBackendErrorMessages::Lookup(Response.ErrorCode)
			: Response.ErrorMessage;
		EndRequest(false, Msg);
		return;
	}

	EndRequest(true, FString());

	// 가입 성공 → 로그인 화면 복귀
	if (LoginWidgetClass)
	{
		AD1MenuPlayerController* PC = Cast<AD1MenuPlayerController>(GetOwningPlayer());
		if (PC)
		{
			PC->SwitchToWidget(LoginWidgetClass);
		}
	}
}
