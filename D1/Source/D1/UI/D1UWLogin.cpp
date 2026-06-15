// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLogin.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Core/D1LogChannels.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Menu/D1MenuPlayerController.h"
#include "Network/BackendErrorMessages.h"
#include "Network/BackendSubsystem.h"

void UD1UWLogin::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginButton)
	{
		LoginButton->OnClicked.AddDynamic(this, &UD1UWLogin::OnLoginClicked);
	}
	if (GotoRegisterButton)
	{
		GotoRegisterButton->OnClicked.AddDynamic(this, &UD1UWLogin::OnGotoRegisterClicked);
	}
}

void UD1UWLogin::OnLoginClicked()
{
	UBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		UE_LOG(LogD1, Error, TEXT("[Login] UBackendSubsystem를 찾을 수 없음"));
		EndRequest(false, FBackendErrorMessages::Lookup(EBackendErrorCode::InternalError));
		return;
	}

	const FString LoginId = LoginIdTextBox ? LoginIdTextBox->GetText().ToString() : FString();
	const FString Password = PasswordTextBox ? PasswordTextBox->GetText().ToString() : FString();

	BeginRequest();
	if (LoginButton)
	{
		LoginButton->SetIsEnabled(false);
	}

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1UWLogin::OnLoginCompletedInternal);
	Backend->Login(LoginId, Password, Cb);
}

void UD1UWLogin::OnGotoRegisterClicked()
{
	if (!RegisterWidgetClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Login] RegisterWidgetClass가 비어있음 (디테일 패널에서 지정 필요)"));
		return;
	}

	AD1MenuPlayerController* PC = Cast<AD1MenuPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->SwitchToWidget(RegisterWidgetClass);
	}
}

void UD1UWLogin::OnLoginCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (LoginButton)
	{
		LoginButton->SetIsEnabled(true);
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

	if (LobbyMap.IsNull())
	{
		UE_LOG(LogD1, Error, TEXT("[Login] LobbyMap이 비어있음 (디테일 패널에서 MP_Lobby 지정 필요)"));
		return;
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, LobbyMap);
}
