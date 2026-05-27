// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWAuthBase.h"

#include "Components/TextBlock.h"

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
