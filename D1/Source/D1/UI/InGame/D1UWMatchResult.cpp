// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InGame/D1UWMatchResult.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Core/D1LogChannels.h"
#include "Network/D1SessionSubsystem.h"
#include "UI/InGame/D1UWMatchResultRow.h"

void UD1UWMatchResult::NativeConstruct()
{
	Super::NativeConstruct();

	// 결과 위젯은 매치 종료 시에만 생성 → 여기서 복귀 카운트다운 시작.
	if (LeaveButton)
	{
		LeaveButton->OnClicked.AddDynamic(this, &UD1UWMatchResult::OnLeaveClicked);
	}

	RemainingSec = FMath::Max(1, FMath::CeilToInt(ReturnCountdownSec));
	if (CountdownLabel)
	{
		CountdownLabel->SetText(FText::AsNumber(RemainingSec));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CountdownTimerHandle, this, &UD1UWMatchResult::TickReturnCountdown, 1.f, /*bLoop=*/true);
	}
}

void UD1UWMatchResult::SetResults(const TArray<FD1MatchResultEntry>& Results)
{
	if (!ResultListPanel)
	{
		return;
	}
	if (!RowWidgetClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[MatchResult] RowWidgetClass가 비어있음 (디테일 패널에서 지정 필요)"));
		return;
	}

	// 등수 오름차순. 미정(0 이하)은 맨 뒤로.
	TArray<FD1MatchResultEntry> Sorted = Results;
	Sorted.Sort([](const FD1MatchResultEntry& A, const FD1MatchResultEntry& B)
	{
		const int32 KeyA = (A.Placement > 0) ? A.Placement : MAX_int32;
		const int32 KeyB = (B.Placement > 0) ? B.Placement : MAX_int32;
		return KeyA < KeyB;
	});

	ResultListPanel->ClearChildren();
	for (const FD1MatchResultEntry& Entry : Sorted)
	{
		UD1UWMatchResultRow* Row = CreateWidget<UD1UWMatchResultRow>(this, RowWidgetClass);
		if (Row)
		{
			Row->SetEntry(Entry);
			ResultListPanel->AddChildToVerticalBox(Row);
		}
	}
}

void UD1UWMatchResult::OnLeaveClicked()
{
	ReturnToLobby();
}

void UD1UWMatchResult::TickReturnCountdown()
{
	--RemainingSec;
	if (CountdownLabel)
	{
		CountdownLabel->SetText(FText::AsNumber(FMath::Max(0, RemainingSec)));
	}

	if (RemainingSec <= 0)
	{
		ReturnToLobby();
	}
}

void UD1UWMatchResult::ReturnToLobby()
{
	if (bReturning)
	{
		return;
	}
	bReturning = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountdownTimerHandle);
	}

	// 이동이 DS 연결을 끊는다 — SessionSubsystem이 의도한 이탈 표시 후 로비 맵(설정 단일 출처)을 연다.
	if (UD1SessionSubsystem* Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UD1SessionSubsystem>() : nullptr)
	{
		Session->TravelToLobby();

		return;
	}

	UE_LOG(LogD1, Error, TEXT("[MatchResult] SessionSubsystem 없음 — 로비 복귀 불가"));
}
