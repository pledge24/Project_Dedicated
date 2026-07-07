// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWRegister.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Core/D1LogChannels.h"
#include "Framework/Menu/D1MenuPlayerController.h"
#include "Network/D1AuthSubsystem.h"

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
	UD1AuthSubsystem* AuthSubsystem = GetAuthSubsystem();
	if (!AuthSubsystem)
	{
		return;
	}

	const FString LoginId = LoginIdTextBox ? LoginIdTextBox->GetText().ToString() : FString();
	const FString Password = PasswordTextBox ? PasswordTextBox->GetText().ToString() : FString();
	const FString Nickname = NicknameTextBox ? NicknameTextBox->GetText().ToString() : FString();

	BeginAuthSubmit();

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1UWRegister::OnRegisterCompletedInternal);
	AuthSubsystem->Register(LoginId, Password, Nickname, Cb);
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
	if (!FinishAuthSubmit(Response))
	{
		return;
	}

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
