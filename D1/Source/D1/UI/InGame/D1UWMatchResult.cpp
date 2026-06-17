// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InGame/D1UWMatchResult.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Core/D1LogChannels.h"
#include "UI/InGame/D1UWMatchResultRow.h"

void UD1UWMatchResult::SetResults(const TArray<FD1MatchResultEntry>& Results)
{
	if (!ResultListPanel || !RowWidgetClass)
	{
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
			CountdownTimerHandle, this, &UD1UWMatchResult::OnCountdownTick, 1.f, /*bLoop=*/true);
	}
}

void UD1UWMatchResult::OnLeaveClicked()
{
	ReturnToLobby();
}

void UD1UWMatchResult::OnCountdownTick()
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

	if (LobbyMap.IsNull())
	{
		UE_LOG(LogD1, Error, TEXT("[MatchResult] LobbyMap이 비어있음 (디테일 패널에서 MP_Lobby 지정 필요)"));
		return;
	}

	// TRAVEL_Absolute → DS 연결 끊고 로컬 MP_Lobby 로드.
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, LobbyMap);
}
