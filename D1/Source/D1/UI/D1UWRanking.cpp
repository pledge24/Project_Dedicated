// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/D1UWRanking.h"

#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1GameInstance.h"
#include "Network/BackendErrorMessages.h"
#include "Network/D1RankingSubsystem.h"
#include "UI/D1UWRankingRow.h"

void UD1UWRanking::NativeConstruct()
{
	Super::NativeConstruct();

	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UD1UWRanking::OnCloseClicked);
	}

	if (UD1RankingSubsystem* Ranking = GetGameInstance()->GetSubsystem<UD1RankingSubsystem>())
	{
		FOnRankingCompleted OnCompleted;
		OnCompleted.BindDynamic(this, &UD1UWRanking::HandleRankingCompleted);
		Ranking->FetchRanking(RankRowCount, 0, OnCompleted);
	}
}

void UD1UWRanking::HandleRankingCompleted(const FBackendResponse& Response, const FD1RankingResult& Result)
{
	if (!Response.bOk)
	{
		// 실패를 빈 목록으로만 보여주면 "아무도 없음"과 구분이 안 된다 — 사유를 표면화한다.
		const FString Msg = FBackendErrorMessages::Resolve(Response);
		UE_LOG(LogD1, Warning, TEXT("[Ranking] 조회 실패 — %s"), *Msg);

		if (ErrorLabel)
		{
			ErrorLabel->SetText(FText::FromString(Msg));
			ErrorLabel->SetVisibility(ESlateVisibility::Visible);
		}
	}
	else if (ErrorLabel)
	{
		ErrorLabel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 실패해도 빈 슬롯으로 채워 레이아웃은 유지.
	PopulateRows(Result);
	ApplyMyRankRow(Result);
}

void UD1UWRanking::PopulateRows(const FD1RankingResult& Result)
{
	if (!RankingScrollBox || !RowWidgetClass)
	{
		return;
	}

	const UD1GameInstance* GI = GetGameInstance<UD1GameInstance>();
	const int32 LocalUserId = GI ? GI->GetCurrentUser().UserId : 0;

	RankingScrollBox->ClearChildren();
	for (int32 i = 0; i < RankRowCount; ++i)
	{
		UD1UWRankingRow* Row = CreateWidget<UD1UWRankingRow>(this, RowWidgetClass);
		if (!Row)
		{
			continue;
		}

		if (i < Result.Entries.Num())
		{
			const FD1RankingEntryDTO& Entry = Result.Entries[i];
			Row->SetEntry(Entry, Entry.UserId == LocalUserId);
		}
		else
		{
			Row->SetEmpty(i + 1);
		}
		RankingScrollBox->AddChild(Row);
	}
}

void UD1UWRanking::ApplyMyRankRow(const FD1RankingResult& Result)
{
	if (!MyRankRow)
	{
		return;
	}

	const UD1GameInstance* GI = GetGameInstance<UD1GameInstance>();
	if (!GI)
	{
		return;
	}
	const FAuthUserDTO& User = GI->GetCurrentUser();

	// me.rank(순위)는 서버 응답, 닉네임/점수는 세션 캐시서 합성(응답 me엔 rank만 있음).
	FD1RankingEntryDTO Mine;
	Mine.Rank     = Result.MyRank;
	Mine.UserId   = User.UserId;
	Mine.Nickname = User.Nickname;
	Mine.Score    = User.Score;
	MyRankRow->SetEntry(Mine, /*bIsLocalPlayer=*/true);
}

void UD1UWRanking::OnCloseClicked()
{
	RemoveFromParent();
}
