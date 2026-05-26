// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1RegisterWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "D1.h"
#include "Menu/D1MenuPlayerController.h"
#include "Online/BackendErrorMessages.h"
#include "Online/BackendSubsystem.h"

void UD1RegisterWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_Register)
	{
		Button_Register->OnClicked.AddDynamic(this, &UD1RegisterWidget::OnRegisterClicked);
	}
	if (Button_BackToLogin)
	{
		Button_BackToLogin->OnClicked.AddDynamic(this, &UD1RegisterWidget::OnBackToLoginClicked);
	}
}

void UD1RegisterWidget::OnRegisterClicked()
{
	UBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		UE_LOG(LogD1, Error, TEXT("[Register] UBackendSubsystem를 찾을 수 없음"));
		EndRequest(false, FBackendErrorMessages::Lookup(EBackendErrorCode::InternalError));
		return;
	}

	const FString LoginId = TextBox_LoginId ? TextBox_LoginId->GetText().ToString() : FString();
	const FString Password = TextBox_Password ? TextBox_Password->GetText().ToString() : FString();
	const FString Nickname = TextBox_Nickname ? TextBox_Nickname->GetText().ToString() : FString();

	BeginRequest();
	if (Button_Register)
	{
		Button_Register->SetIsEnabled(false);
	}

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1RegisterWidget::OnRegisterCompletedInternal);
	Backend->Register(LoginId, Password, Nickname, Cb);
}

void UD1RegisterWidget::OnBackToLoginClicked()
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

void UD1RegisterWidget::OnRegisterCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (Button_Register)
	{
		Button_Register->SetIsEnabled(true);
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
