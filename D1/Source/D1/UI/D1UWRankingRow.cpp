// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWRankingRow.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"

void UD1UWRankingRow::SetEntry(const FD1RankingEntryDTO& Entry, bool bIsLocalPlayer)
{
	if (RankLabel)
	{
		RankLabel->SetText(FText::AsNumber(Entry.Rank));
	}
	if (NicknameLabel)
	{
		NicknameLabel->SetText(FText::FromString(Entry.Nickname));
	}
	if (ScoreLabel)
	{
		ScoreLabel->SetText(FText::AsNumber(Entry.Score));
	}
	if (RowBorder)
	{
		RowBorder->SetBrushColor(bIsLocalPlayer ? LocalHighlightColor : FLinearColor::Transparent);
	}
}

void UD1UWRankingRow::SetEmpty(int32 Rank)
{
	if (RankLabel)
	{
		RankLabel->SetText(FText::AsNumber(Rank));
	}
	if (NicknameLabel)
	{
		NicknameLabel->SetText(FText::FromString(TEXT("-")));
	}
	if (ScoreLabel)
	{
		ScoreLabel->SetText(FText::FromString(TEXT("-")));
	}
	if (RowBorder)
	{
		RowBorder->SetBrushColor(FLinearColor::Transparent);
	}
}
