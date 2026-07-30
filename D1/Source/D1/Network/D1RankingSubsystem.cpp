// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1RankingSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"

void UD1RankingSubsystem::FetchRanking(int32 Limit, int32 Offset, const FOnRankingCompleted& OnCompleted)
{
	FBackendResponse Out;
	FD1RankingResult Result;

	const FString Jwt = D1BackendHttp::GetSessionJwt(GetGameInstance());
	if (Jwt.IsEmpty())
	{
		// 비로그인 — 조회할 세션 없음.
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::NetworkError;
		OnCompleted.ExecuteIfBound(Out, Result);
		return;
	}

	const FString Path = FString::Printf(TEXT("/api/ranking?limit=%d&offset=%d"), Limit, Offset);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(GetGameInstance(), Path, /*bAttachAuth=*/true);

	TWeakObjectPtr<UD1RankingSubsystem> WeakThis(this);
	const FOnRankingCompleted Forward = OnCompleted;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Forward](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UD1RankingSubsystem* Self = WeakThis.Get())
			{
				Self->HandleRankingResponse(Req, Res, bSucceeded, Forward);
			}
		});
	Request->ProcessRequest();
}

void UD1RankingSubsystem::HandleRankingResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded, FOnRankingCompleted Forward)
{
	FBackendResponse Out;
	FD1RankingResult Result;

	// fail case: 네트워크 자체 실패
	if (!bSucceeded || !Res.IsValid())
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::NetworkError;
		UE_LOG(LogD1, Warning, TEXT("[Ranking] HTTP 실패 (네트워크 또는 응답 없음)"));
		Forward.ExecuteIfBound(Out, Result);
		return;
	}

	// 옛 기기의 랭킹 요청도 401 SESSION_SUPERSEDED면 즉시 로그인 복귀(빈 목록 대신 팝업).
	if (D1BackendHttp::HandleSupersededIfAny(GetGameInstance(), Res))
	{
		return;
	}

	const FString Content = Res->GetContentAsString();
	TSharedPtr<FJsonObject> Root;
	// fail case: content 역직렬화 실패
	if (!D1BackendHttp::DeserializeJson(Content, Root))
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		UE_LOG(LogD1, Warning, TEXT("[Ranking] JSON 역직렬화 실패: %s"), *Content);
		Forward.ExecuteIfBound(Out, Result);
		return;
	}

	const bool bOk = Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok"));
	if (bOk)
	{
		const TSharedPtr<FJsonObject>* DataObj = nullptr;
		if (D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
		{
			// entries[] 순회 — 서버 키는 camelCase.
			const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
			if ((*DataObj)->TryGetArrayField(TEXT("entries"), EntriesArr))
			{
				for (const TSharedPtr<FJsonValue>& Value : *EntriesArr)
				{
					const TSharedPtr<FJsonObject> RowObj = Value->AsObject();
					if (!RowObj.IsValid())
					{
						continue;
					}

					FD1RankingEntryDTO Entry;
					Entry.Rank     = static_cast<int32>(RowObj->GetNumberField(TEXT("rank")));
					Entry.UserId   = static_cast<int32>(RowObj->GetNumberField(TEXT("userId")));
					Entry.Nickname = RowObj->GetStringField(TEXT("nickname"));
					Entry.Score    = static_cast<int32>(RowObj->GetNumberField(TEXT("score")));
					RowObj->TryGetNumberField(TEXT("level"),         Entry.Level);
					RowObj->TryGetNumberField(TEXT("wins"),          Entry.Wins);
					RowObj->TryGetNumberField(TEXT("losses"),        Entry.Losses);
					RowObj->TryGetNumberField(TEXT("matchesPlayed"), Entry.MatchesPlayed);
					Result.Entries.Add(Entry);
				}
			}

			// me.rank — 본인 전역 순위(닉네임/점수는 응답에 없음).
			const TSharedPtr<FJsonObject>* MeObj = nullptr;
			if (D1BackendHttp::GetObjectField(*DataObj, TEXT("me"), MeObj))
			{
				(*MeObj)->TryGetNumberField(TEXT("rank"), Result.MyRank);
			}

			const TSharedPtr<FJsonObject>* MetaObj = nullptr;
			if (D1BackendHttp::GetObjectField(*DataObj, TEXT("meta"), MetaObj))
			{
				(*MetaObj)->TryGetNumberField(TEXT("total"), Result.Total);
			}

			Out.bOk = true;
			Out.ErrorCode = EBackendErrorCode::None;
			Forward.ExecuteIfBound(Out, Result);
			return;
		}

		// fail case: 성공 envelope + data 필드 없음.
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		Out.ErrorMessage = TEXT("성공한 요청에 데이터를 가져올 수 없습니다");
		Forward.ExecuteIfBound(Out, Result);
		return;
	}

	// fail case: 실패 envelope
	const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
	if (D1BackendHttp::GetObjectField(Root, TEXT("error"), ErrorObj))
	{
		const FString CodeStr = (*ErrorObj)->GetStringField(TEXT("code"));
		Out.ErrorCode    = D1BackendHttp::ParseErrorCode(CodeStr);
		Out.ErrorMessage = (*ErrorObj)->GetStringField(TEXT("message"));
	}
	else
	{
		Out.ErrorCode = EBackendErrorCode::Unknown;
	}
	Out.bOk = false;
	Forward.ExecuteIfBound(Out, Result);
}
