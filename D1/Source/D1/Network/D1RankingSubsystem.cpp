// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1RankingSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"

void UD1RankingSubsystem::FetchRanking(int32 Limit, int32 Offset, const FOnRankingCompleted& OnCompleted)
{
	if (D1BackendHttp::GetSessionJwt(GetGameInstance()).IsEmpty())
	{
		// 비로그인 — 조회할 세션 없음.
		FBackendResponse Out;
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::NotAuthenticated;
		OnCompleted.ExecuteIfBound(Out, FD1RankingResult());
		return;
	}

	const FString Path = FString::Printf(TEXT("/api/ranking?limit=%d&offset=%d"), Limit, Offset);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(
		GetGameInstance(), Path, D1BackendHttp::EBackendAuth::SessionJwt);
	D1BackendHttp::SendAsync(this, Request,
		[this, Forward = OnCompleted](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			HandleRankingResponse(Res, bSucceeded, Forward);
		});
}

void UD1RankingSubsystem::HandleRankingResponse(const FHttpResponsePtr& Res, bool bSucceeded, FOnRankingCompleted Forward)
{
	FBackendResponse Out;
	FD1RankingResult Result;

	TSharedPtr<FJsonObject> Data;
	const D1BackendHttp::EEnvelopeOutcome Outcome = D1BackendHttp::ParseEnvelope(GetGameInstance(), Res, bSucceeded, Out, Data);
	if (Outcome == D1BackendHttp::EEnvelopeOutcome::Superseded)
	{
		// 세션 대체 — 빈 목록 콜백 대신 SessionSubsystem의 화면 복귀에 맡긴다.
		return;
	}
	if (Outcome == D1BackendHttp::EEnvelopeOutcome::Failed)
	{
		Forward.ExecuteIfBound(Out, Result);
		return;
	}

	// entries[] 순회 — 서버 키는 camelCase.
	const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
	if (Data->TryGetArrayField(TEXT("entries"), EntriesArr))
	{
		for (const TSharedPtr<FJsonValue>& Value : *EntriesArr)
		{
			const TSharedPtr<FJsonObject> RowObj = Value->AsObject();
			if (!RowObj.IsValid())
			{
				continue;
			}

			FD1RankingEntryDTO Entry;
			RowObj->TryGetNumberField(TEXT("rank"),          Entry.Rank);
			RowObj->TryGetNumberField(TEXT("userId"),        Entry.UserId);
			RowObj->TryGetStringField(TEXT("nickname"),      Entry.Nickname);
			RowObj->TryGetNumberField(TEXT("score"),         Entry.Score);
			RowObj->TryGetNumberField(TEXT("level"),         Entry.Level);
			RowObj->TryGetNumberField(TEXT("wins"),          Entry.Wins);
			RowObj->TryGetNumberField(TEXT("losses"),        Entry.Losses);
			RowObj->TryGetNumberField(TEXT("matchesPlayed"), Entry.MatchesPlayed);
			Result.Entries.Add(Entry);
		}
	}

	// me.rank — 본인 전역 순위(닉네임/점수는 응답에 없음).
	const TSharedPtr<FJsonObject>* MeObj = nullptr;
	if (D1BackendHttp::GetObjectField(Data, TEXT("me"), MeObj))
	{
		(*MeObj)->TryGetNumberField(TEXT("rank"), Result.MyRank);
	}

	const TSharedPtr<FJsonObject>* MetaObj = nullptr;
	if (D1BackendHttp::GetObjectField(Data, TEXT("meta"), MetaObj))
	{
		(*MetaObj)->TryGetNumberField(TEXT("total"), Result.Total);
	}

	Forward.ExecuteIfBound(Out, Result);
}
