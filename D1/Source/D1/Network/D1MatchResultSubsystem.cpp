// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1MatchResultSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"

void UD1MatchResultSubsystem::ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
	int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("matchId"),     MatchId);
	Body->SetStringField(TEXT("mapName"),     MapName);
	Body->SetNumberField(TEXT("durationSec"), DurationSec);
	Body->SetStringField(TEXT("endReason"),   EndReason);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FMatchResultPlayer& P : Players)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("userId"),    static_cast<double>(P.UserId));
		Entry->SetNumberField(TEXT("slotIndex"), P.SlotIndex);
		Entry->SetNumberField(TEXT("placement"), P.Placement);
		Entry->SetNumberField(TEXT("livesLeft"), P.LivesLeft);
		Entry->SetBoolField(TEXT("left"),        P.Left);
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Body->SetArrayField(TEXT("results"), Results);

	// 매치별 서버 토큰을 Bearer로 — 유저 JWT가 아니라 DS 인증 채널. (BuildPostJson은 auth 미첨부로 호출.)
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(GetGameInstance(), TEXT("/api/match/result"), Body, /*bAttachAuth=*/false);
	Request->SetHeader(TEXT("Authorization"), D1BackendHttp::MakeBearer(MatchToken));

	Request->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			const int32 Code = (bSucceeded && Res.IsValid()) ? Res->GetResponseCode() : 0;
			if (Code == 200)
			{
				UE_LOG(LogD1, Log, TEXT("[Match] 결과 POST 성공 (200)"));
			}
			else
			{
				const FString Content = (bSucceeded && Res.IsValid()) ? Res->GetContentAsString() : TEXT("(no response)");
				UE_LOG(LogD1, Warning, TEXT("[Match] 결과 POST 실패 code=%d %s"), Code, *Content);
			}
		});
	Request->ProcessRequest();

	UE_LOG(LogD1, Log, TEXT("[Match] 결과 POST 전송 matchId=%s reason=%s players=%d"), *MatchId, *EndReason, Players.Num());
}

void UD1MatchResultSubsystem::ReportLeaver(const FString& MatchId, const FString& MatchToken, int64 UserId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("userId"), static_cast<double>(UserId));

	const FString Path = FString::Printf(TEXT("/api/match/%s/leaver"), *MatchId);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(GetGameInstance(), Path, Body, /*bAttachAuth=*/false);
	Request->SetHeader(TEXT("Authorization"), D1BackendHttp::MakeBearer(MatchToken));

	Request->OnProcessRequestComplete().BindLambda(
		[UserId](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			const int32 Code = (bSucceeded && Res.IsValid()) ? Res->GetResponseCode() : 0;
			if (Code != 200)
			{
				const FString Content = (bSucceeded && Res.IsValid()) ? Res->GetContentAsString() : TEXT("(no response)");
				UE_LOG(LogD1, Warning, TEXT("[Match] 탈주 정산 POST 실패 userId=%lld code=%d %s"), UserId, Code, *Content);
			}
		});
	Request->ProcessRequest();

	UE_LOG(LogD1, Log, TEXT("[Match] 탈주 즉시 정산 POST userId=%lld matchId=%s"), UserId, *MatchId);
}
