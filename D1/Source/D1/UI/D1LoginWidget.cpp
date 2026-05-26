// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1LoginWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "D1.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/D1MenuPlayerController.h"
#include "Online/BackendErrorMessages.h"
#include "Online/BackendSubsystem.h"

void UD1LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_Login)
	{
		Button_Login->OnClicked.AddDynamic(this, &UD1LoginWidget::OnLoginClicked);
	}
	if (Button_GotoRegister)
	{
		Button_GotoRegister->OnClicked.AddDynamic(this, &UD1LoginWidget::OnGotoRegisterClicked);
	}
}

void UD1LoginWidget::OnLoginClicked()
{
	UBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UBackendSubsystem>() : nullptr;
	if (!Backend)
	{
		UE_LOG(LogD1, Error, TEXT("[Login] UBackendSubsystem를 찾을 수 없음"));
		EndRequest(false, FBackendErrorMessages::Lookup(EBackendErrorCode::InternalError));
		return;
	}

	const FString LoginId = TextBox_LoginId ? TextBox_LoginId->GetText().ToString() : FString();
	const FString Password = TextBox_Password ? TextBox_Password->GetText().ToString() : FString();

	BeginRequest();
	if (Button_Login)
	{
		Button_Login->SetIsEnabled(false);
	}

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1LoginWidget::OnLoginCompletedInternal);
	Backend->Login(LoginId, Password, Cb);
}

void UD1LoginWidget::OnGotoRegisterClicked()
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

void UD1LoginWidget::OnLoginCompletedInternal(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (Button_Login)
	{
		Button_Login->SetIsEnabled(true);
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
