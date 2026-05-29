// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/InGame/D1UWMatchResultRow.h"

#include "Components/TextBlock.h"

void UD1UWMatchResultRow::SetEntry(const FD1MatchResultEntry& Entry)
{
	if (PlacementLabel)
	{
		// 등수 미정(0 이하)은 "-"로 표기.
		const FText PlacementText = (Entry.Placement > 0)
			? FText::Format(NSLOCTEXT("MatchResult", "PlacementFmt", "{0}위"), FText::AsNumber(Entry.Placement))
			: FText::FromString(TEXT("-"));
		PlacementLabel->SetText(PlacementText);
	}
	if (NicknameLabel)
	{
		NicknameLabel->SetText(FText::FromString(Entry.Nickname));
	}
}
