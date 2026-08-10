// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWLogin.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Core/D1LogChannels.h"
#include "Framework/Menu/D1MenuPlayerController.h"
#include "Network/D1AuthSubsystem.h"
#include "Network/D1SessionSubsystem.h"

void UD1UWLogin::NativeConstruct()
{
	Super::NativeConstruct();

	// 필수 BindWidget — WBP 컴파일러가 누락을 에러로 차단하므로 null 불가.
	LoginButton->OnClicked.AddDynamic(this, &UD1UWLogin::OnLoginClicked);
	GotoRegisterButton->OnClicked.AddDynamic(this, &UD1UWLogin::OnGotoRegisterClicked);
}

void UD1UWLogin::OnLoginClicked()
{
	UD1AuthSubsystem* AuthSubsystem = GetAuthSubsystem();
	if (!AuthSubsystem)
	{
		return;
	}

	const FString LoginId = LoginIdTextBox->GetText().ToString();
	const FString Password = PasswordTextBox->GetText().ToString();

	BeginAuthSubmit();

	FOnAuthCompleted Cb;
	Cb.BindDynamic(this, &UD1UWLogin::HandleLoginCompleted);
	AuthSubsystem->Login(LoginId, Password, Cb);
}

void UD1UWLogin::HandleLoginCompleted(const FBackendResponse& Response, const FAuthUserDTO& User)
{
	if (!FinishAuthSubmit(Response))
	{
		return;
	}

	if (UD1SessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UD1SessionSubsystem>() : nullptr)
	{
		Session->TravelToLobby();
	}
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
