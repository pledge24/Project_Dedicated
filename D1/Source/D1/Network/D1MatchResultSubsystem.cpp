// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1MatchResultSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"
#include "TimerManager.h"

void UD1MatchResultSubsystem::ReportDSReady(const FString& MatchId, const FString& MatchToken)
{
	FD1ReportPolicy Policy;
	Policy.Label = TEXT("준비");
	Policy.MatchId = MatchId;
	// 1,2,3,4초 백오프(누적 10s) — 전부 백엔드 준비 타임아웃(30s) 안.
	Policy.RetryDelaysSec = { 1.f, 2.f, 3.f, 4.f };

	// 빈 바디({}) — matchId는 경로, 인증은 매치별 서버 토큰. 백엔드는 이 도착만으로 "DS 준비됨" 판정.
	const FString Path = FString::Printf(TEXT("/api/match/%s/ready"), *MatchId);
	SendReport(Path, MatchToken, MakeShared<FJsonObject>(), /*Attempt=*/0, Policy, ServerReadyRetryTimerHandle);
}

void UD1MatchResultSubsystem::ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
	int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players,
	const FSimpleDelegate& OnSettled)
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

	FD1ReportPolicy Policy;
	Policy.Label = TEXT("결과");
	Policy.MatchId = MatchId;
	// 1,2,4,8,15초(누적 30초) — 준비 POST(누적 10초)보다 길게 잡는다. 그쪽은 백엔드의 30초
	// 준비 타임아웃 안에 들어야 하지만, 이쪽이 넘어야 하는 창은 백엔드의 재기동 시간이다.
	Policy.RetryDelaysSec = { 1.f, 2.f, 4.f, 8.f, 15.f };
	// 409 = 백엔드가 client_match_id UNIQUE로 이미 저장한 재전송 → 성공과 동일 확정.
	Policy.bTreat409AsSettled = true;
	Policy.bLogLossAsError = true;
	Policy.OnSettled = OnSettled;

	UE_LOG(LogD1, Log, TEXT("[Match] 결과 POST 시작 matchId=%s reason=%s players=%d"), *MatchId, *EndReason, Players.Num());
	SendReport(TEXT("/api/match/result"), MatchToken, Body, /*Attempt=*/0, Policy, MatchResultRetryTimerHandle);
}

void UD1MatchResultSubsystem::ReportLeaver(const FString& MatchId, const FString& MatchToken, int64 UserId)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("userId"), static_cast<double>(UserId));

	const FString Path = FString::Printf(TEXT("/api/match/%s/leaver"), *MatchId);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(
		GetGameInstance(), Path, Body, D1BackendHttp::EBackendAuth::ServerToken, MatchToken);

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

void UD1MatchResultSubsystem::FetchKicks(const FString& MatchId, const FString& MatchToken,
	TFunction<void(const TArray<int64>&)> OnKicked)
{
	const FString Path = FString::Printf(TEXT("/api/match/%s/kicks"), *MatchId);
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(
		GetGameInstance(), Path, D1BackendHttp::EBackendAuth::ServerToken, MatchToken);

	D1BackendHttp::SendAsync(this, Request,
		[OnKicked = MoveTemp(OnKicked)](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			// 폴링 1회 실패는 무시 — 다음 주기가 곧 온다.
			if (!bSucceeded || !Res.IsValid() || Res->GetResponseCode() != 200)
			{
				return;
			}

			TSharedPtr<FJsonObject> Root;
			if (!D1BackendHttp::DeserializeJson(Res->GetContentAsString(), Root))
			{
				return;
			}

			const TSharedPtr<FJsonObject>* DataObj = nullptr;
			if (!D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
			{
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* UserIdValues = nullptr;
			if (!(*DataObj)->TryGetArrayField(TEXT("userIds"), UserIdValues))
			{
				return;
			}

			TArray<int64> UserIds;
			for (const TSharedPtr<FJsonValue>& Value : *UserIdValues)
			{
				if (Value.IsValid())
				{
					UserIds.Add(static_cast<int64>(Value->AsNumber()));
				}
			}
			OnKicked(UserIds);
		});
}

void UD1MatchResultSubsystem::SendReport(const FString& Path, const FString& MatchToken,
	const TSharedRef<FJsonObject>& Body, int32 Attempt, const FD1ReportPolicy& Policy, FTimerHandle& RetryTimerHandle)
{
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(
		GetGameInstance(), Path, Body, D1BackendHttp::EBackendAuth::ServerToken, MatchToken);

	TWeakObjectPtr<UD1MatchResultSubsystem> WeakThis(this);
	FTimerHandle* TimerHandlePtr = &RetryTimerHandle;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Path, MatchToken, Body, Attempt, Policy, TimerHandlePtr](FHttpRequestPtr, FHttpResponsePtr Res, bool bSucceeded)
		{
			const int32 Code = (bSucceeded && Res.IsValid()) ? Res->GetResponseCode() : 0;

			if (Code == 200 || (Policy.bTreat409AsSettled && Code == 409))
			{
				UE_LOG(LogD1, Log, TEXT("[Match] %s POST 확정 code=%d matchId=%s attempt=%d"),
					*Policy.Label, Code, *Policy.MatchId, Attempt);
				Policy.OnSettled.ExecuteIfBound();

				return;
			}

			// 확정적 4xx: 백엔드가 내용을 보고 거부한 것 — 재전송해도 판정이 바뀌지 않는다.
			if (Code >= 400 && Code < 500)
			{
				const FString Content = Res.IsValid() ? Res->GetContentAsString() : TEXT("(no response)");
				if (Policy.bLogLossAsError)
				{
					UE_LOG(LogD1, Error, TEXT("[Match] %s POST 거부 code=%d %s — 재시도 안 함(결과 유실) matchId=%s"),
						*Policy.Label, Code, *Content, *Policy.MatchId);
				}
				else
				{
					UE_LOG(LogD1, Warning, TEXT("[Match] %s POST 거부 code=%d %s — 재시도 안 함 matchId=%s"),
						*Policy.Label, Code, *Content, *Policy.MatchId);
				}
				Policy.OnSettled.ExecuteIfBound();

				return;
			}

			// 일시 실패(전송 실패/0/5xx/429): 상한까지 백오프 재시도. 준비 signal·결과 저장 모두 멱등이라 중복 무해.
			UD1MatchResultSubsystem* Self = WeakThis.Get();
			UGameInstance* GI = Self ? Self->GetGameInstance() : nullptr;
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			if (!Self || !World || Attempt >= Policy.RetryDelaysSec.Num())
			{
				if (Policy.bLogLossAsError)
				{
					UE_LOG(LogD1, Error, TEXT("[Match] %s POST 재시도 종료 code=%d attempt=%d — 결과 유실 matchId=%s"),
						*Policy.Label, Code, Attempt, *Policy.MatchId);
				}
				else
				{
					UE_LOG(LogD1, Warning, TEXT("[Match] %s POST 재시도 종료 code=%d attempt=%d matchId=%s"),
						*Policy.Label, Code, Attempt, *Policy.MatchId);
				}
				Policy.OnSettled.ExecuteIfBound();

				return;
			}

			const int32 NextAttempt = Attempt + 1;
			const float RetryDelaySec = Policy.RetryDelaysSec[Attempt];
			UE_LOG(LogD1, Warning, TEXT("[Match] %s POST 일시 실패 code=%d — %.0fs 후 재시도(%d/%d)"),
				*Policy.Label, Code, RetryDelaySec, NextAttempt, Policy.RetryDelaysSec.Num());
			World->GetTimerManager().SetTimer(*TimerHandlePtr,
				FTimerDelegate::CreateWeakLambda(Self, [Self, Path, MatchToken, Body, NextAttempt, Policy, TimerHandlePtr]()
				{
					Self->SendReport(Path, MatchToken, Body, NextAttempt, Policy, *TimerHandlePtr);
				}),
				RetryDelaySec, /*bLoop=*/false);
		});
	Request->ProcessRequest();

	UE_LOG(LogD1, Log, TEXT("[Match] %s POST 전송 matchId=%s attempt=%d"), *Policy.Label, *Policy.MatchId, Attempt);
}
