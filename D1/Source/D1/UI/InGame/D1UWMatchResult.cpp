// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InGame/D1UWMatchResult.h"

#include "Components/VerticalBox.h"
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
