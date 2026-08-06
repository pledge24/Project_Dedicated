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

	// 필수 BindWidget — WBP 컴파일러가 누락을 에러로 차단하므로 null 불가.
	RegisterButton->OnClicked.AddDynamic(this, &UD1UWRegister::OnRegisterClicked);
	BackToLoginButton->OnClicked.AddDynamic(this, &UD1UWRegister::OnBackToLoginClicked);
}

void UD1UWRegister::OnRegisterClicked()
{
	UD1AuthSubsystem* AuthSubsystem = GetAuthSubsystem();
	if (!AuthSubsystem)
	{
		return;
	}

	const FString LoginId = LoginIdTextBox->GetText().ToString();
	const FString Password = PasswordTextBox->GetText().ToString();
	const FString Nickname = NicknameTextBox->GetText().ToString();

	BeginAuthSubmit();

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1UWRegister::HandleRegisterCompleted);
	AuthSubsystem->Register(LoginId, Password, Nickname, Cb);
}

void UD1UWRegister::HandleRegisterCompleted(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (!FinishAuthSubmit(Response))
	{
		return;
	}

	if (LoginWidgetClass)
	{
		AD1MenuPlayerController* PC = Cast<AD1MenuPlayerController>(GetOwningPlayer());
		if (PC)
		{
			PC->SwitchToWidget(LoginWidgetClass);
		}
	}
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
