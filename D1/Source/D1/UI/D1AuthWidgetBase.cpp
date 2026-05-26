// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1AuthWidgetBase.h"

#include "Components/TextBlock.h"

void UD1AuthWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (TextBlock_Error)
	{
		TextBlock_Error->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UD1AuthWidgetBase::BeginRequest()
{
	if (TextBlock_Error)
	{
		TextBlock_Error->SetText(FText::GetEmpty());
		TextBlock_Error->SetVisibility(ESlateVisibility::Collapsed);
	}
	OnRequestStarted();
}

void UD1AuthWidgetBase::EndRequest(bool bSuccess, const FString& ErrorMessage)
{
	if (!bSuccess && TextBlock_Error)
	{
		TextBlock_Error->SetText(FText::FromString(ErrorMessage));
		TextBlock_Error->SetVisibility(ESlateVisibility::Visible);
	}
	OnRequestFinished(bSuccess, ErrorMessage);
}
