// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWAuthBase.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Core/D1LogChannels.h"
#include "Network/BackendErrorMessages.h"
#include "Network/D1AuthSubsystem.h"

void UD1UWAuthBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (ErrorLabel)
	{
		ErrorLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UD1UWAuthBase::BeginRequest()
{
	if (ErrorLabel)
	{
		ErrorLabel->SetText(FText::GetEmpty());
		ErrorLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	OnRequestStarted();
}

void UD1UWAuthBase::EndRequest(bool bSuccess, const FString& ErrorMessage)
{
	if (!bSuccess && ErrorLabel)
	{
		ErrorLabel->SetText(FText::FromString(ErrorMessage));
		ErrorLabel->SetVisibility(ESlateVisibility::Visible);
	}
	OnRequestFinished(bSuccess, ErrorMessage);
}

UD1AuthSubsystem* UD1UWAuthBase::GetAuthSubsystem()
{
	UD1AuthSubsystem* AuthSystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UD1AuthSubsystem>() : nullptr;
	if (!AuthSystem)
	{
		UE_LOG(LogD1, Error, TEXT("[Auth] UD1AuthSubsystem을 찾을 수 없음"));
		EndRequest(false, FBackendErrorMessages::Lookup(EBackendErrorCode::InternalError));
	}
	return AuthSystem;
}

void UD1UWAuthBase::BeginAuthSubmit()
{
	BeginRequest();
	if (UButton* SubmitButton = GetSubmitButton())
	{
		SubmitButton->SetIsEnabled(false);
	}
}

bool UD1UWAuthBase::FinishAuthSubmit(const FBackendResponse& Response)
{
	if (UButton* SubmitButton = GetSubmitButton())
	{
		SubmitButton->SetIsEnabled(true);
	}

	if (!Response.bOk)
	{
		const FString Msg = Response.ErrorMessage.IsEmpty()
			? FBackendErrorMessages::Lookup(Response.ErrorCode)
			: Response.ErrorMessage;
		EndRequest(false, Msg);
		return false;
	}

	EndRequest(true, FString());
	return true;
}
