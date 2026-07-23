// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1MatchResultSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"
#include "TimerManager.h"

void UD1MatchResultSubsystem::ReportDSReady(const FString& MatchId, const FString& MatchToken)
{
	SendServerReady(MatchId, MatchToken, /*Attempt=*/0);
}

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

void UD1MatchResultSubsystem::SendServerReady(const FString& MatchId, const FString& MatchToken, int32 Attempt)
{
	// 빈 바디({}) — matchId는 경로, 인증은 Bearer(매치별 서버 토큰). 백엔드는 이 도착만으로 "DS 준비됨" 판정.
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();

	const FString Path = FString::Printf(TEXT("/api/match/%s/ready"), *MatchId);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(GetGameInstance(), Path, Body, /*bAttachAuth=*/false);
	Request->SetHeader(TEXT("Authorization"), D1BackendHttp::MakeBearer(MatchToken));

	TWeakObjectPtr<UD1MatchResultSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, MatchId, MatchToken, Attempt](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			const int32 Code = (bSucceeded && Res.IsValid()) ? Res->GetResponseCode() : 0;
			if (Code == 200)
			{
				UE_LOG(LogD1, Log, TEXT("[Match] 준비 POST 성공 (200) matchId=%s"), *MatchId);

				return;
			}

			// 확정적 4xx(매치 없음/토큰 불일치): 백엔드가 이미 포기 → 재시도 무의미. 로그만.
			if (Code >= 400 && Code < 500)
			{
				const FString Content = Res.IsValid() ? Res->GetContentAsString() : TEXT("(no response)");
				UE_LOG(LogD1, Warning, TEXT("[Match] 준비 POST 거부 code=%d %s — 재시도 안 함"), Code, *Content);

				return;
			}

			// 일시 실패(전송 실패/0/5xx/429): 상한까지 백오프 재시도. 백엔드 signal이 멱등이라 중복 성공도 무해.
			UD1MatchResultSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			constexpr int32 MaxRetries = 4;
			if (Attempt >= MaxRetries)
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] 준비 POST 실패 code=%d — 재시도 소진(attempt=%d)"), Code, Attempt);

				return;
			}

			UGameInstance* GI = Self->GetGameInstance();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			if (!World)
			{
				return;
			}

			// 1,2,3,4초 백오프(누적 0,1,3,6,10s) — 전부 백엔드 준비 타임아웃(30s) 안.
			const int32 NextAttempt = Attempt + 1;
			const float RetryDelaySec = static_cast<float>(NextAttempt);
			UE_LOG(LogD1, Log, TEXT("[Match] 준비 POST 일시 실패 code=%d — %.0fs 후 재시도(%d/%d)"),
				Code, RetryDelaySec, NextAttempt, MaxRetries);
			World->GetTimerManager().SetTimer(Self->ServerReadyRetryTimerHandle,
				FTimerDelegate::CreateWeakLambda(Self, [Self, MatchId, MatchToken, NextAttempt]()
				{
					Self->SendServerReady(MatchId, MatchToken, NextAttempt);
				}),
				RetryDelaySec, /*bLoop=*/false);
		});
	Request->ProcessRequest();

	UE_LOG(LogD1, Log, TEXT("[Match] 준비 POST 전송 matchId=%s attempt=%d"), *MatchId, Attempt);
}
