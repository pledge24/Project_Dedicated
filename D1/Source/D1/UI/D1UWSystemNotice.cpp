// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWSystemNotice.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

void UD1UWSystemNotice::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.AddDynamic(this, &UD1UWSystemNotice::OnConfirmClicked);
	}

	// SetNotice가 AddToViewport(=Construct) 전에 불렸으면 여기서 보류분 반영.
	ApplyPending();
}

void UD1UWSystemNotice::SetNotice(const FText& InTitle, const FText& InMessage)
{
	PendingTitle = InTitle;
	PendingMessage = InMessage;
	bHasPending = true;
	ApplyPending();
}

void UD1UWSystemNotice::OnConfirmClicked()
{
	// 실제 세션 정리·화면 복귀는 구독자(SessionSubsystem)가 담당.
	OnConfirmed.Broadcast();
}

void UD1UWSystemNotice::ApplyPending()
{
	if (!bHasPending)
	{
		return;
	}

	if (TitleLabel)
	{
		TitleLabel->SetText(PendingTitle);
	}
	if (MessageLabel)
	{
		MessageLabel->SetText(PendingMessage);
	}
}
