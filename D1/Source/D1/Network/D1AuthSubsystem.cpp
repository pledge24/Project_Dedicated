// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1AuthSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Framework/D1GameInstance.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"

void UD1AuthSubsystem::Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("nickname"), Nickname);

	SendAuthRequest(TEXT("/api/auth/register"), Body, OnCompleted);
}

void UD1AuthSubsystem::Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);

	SendAuthRequest(TEXT("/api/auth/login"), Body, OnCompleted);
}

void UD1AuthSubsystem::SendAuthRequest(const FString& Path, const TSharedRef<FJsonObject>& Body, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(
		GetGameInstance(), Path, Body, D1BackendHttp::EBackendAuth::None);
	D1BackendHttp::SendAsync(this, Request,
		[this, Forward = OnCompleted](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			HandleAuthResponse(Res, bSucceeded, Forward);
		});
}

void UD1AuthSubsystem::HandleAuthResponse(const FHttpResponsePtr& Res, bool bSucceeded, FOnAuthCompleted Forward)
{
	FBackendResponse Out;
	FAuthUserDTO User;

	TSharedPtr<FJsonObject> Data;
	const D1BackendHttp::EEnvelopeOutcome Outcome = D1BackendHttp::ParseEnvelope(GetGameInstance(), Res, bSucceeded, Out, Data);
	if (Outcome == D1BackendHttp::EEnvelopeOutcome::Superseded)
	{
		return;
	}
	if (Outcome == D1BackendHttp::EEnvelopeOutcome::Failed)
	{
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	D1BackendHttp::ParseAuthUser(Data, User);

	// register 응답은 token이 없다 (서버가 가입 직후 자동 로그인을 막음).
	// token이 비어있으면 세션을 만들지 않고, 클라는 별도로 Login을 호출해야 한다.
	FString Token;
	Data->TryGetStringField(TEXT("token"), Token);
	if (!Token.IsEmpty())
	{
		// 조용히 스킵하면 "로그인 성공인데 세션 없음"이 되어 이후 전 인증 요청이 무너진다.
		// GameInstanceClass 미스컨피그의 최조기 검출기(매 세션 첫 로그인 경로).
		UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance());
		if (ensureMsgf(GI, TEXT("[Auth] GameInstanceClass가 UD1GameInstance 아님 — 세션 저장 불가")))
		{
			GI->SetSession(Token, User);
		}
	}

	Forward.ExecuteIfBound(Out, User);
}

void UD1AuthSubsystem::RefreshMyProfile()
{
	if (D1BackendHttp::GetSessionJwt(GetGameInstance()).IsEmpty())
	{
		return;
	}

	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(
		GetGameInstance(), TEXT("/api/auth/me"), D1BackendHttp::EBackendAuth::SessionJwt);
	D1BackendHttp::SendAsync(this, Request,
		[this](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			HandleProfileResponse(Res, bSucceeded);
		});
}

void UD1AuthSubsystem::HandleProfileResponse(const FHttpResponsePtr& Res, bool bSucceeded)
{
	FBackendResponse Out;
	TSharedPtr<FJsonObject> Data;
	// 실패는 조용히 — 캐시 유지, 다음 갱신 기회에. 세션 대체는 ParseEnvelope이 화면 복귀까지 처리.
	if (D1BackendHttp::ParseEnvelope(GetGameInstance(), Res, bSucceeded, Out, Data) != D1BackendHttp::EEnvelopeOutcome::Ok)
	{
		return;
	}

	// login 응답과 동일 모양 — score/level/exp 최신값. token은 없음(세션은 그대로 유지).
	FAuthUserDTO User;
	D1BackendHttp::ParseAuthUser(Data, User);

	// cast 실패인데 방송·성공 로그까지 가면 UI가 낡은 캐시를 최신으로 오인한다 — 도달만 차단.
	// 미스컨피그 검출은 로그인 경로의 ensure(근원 검출기)가 담당.
	UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}
	GI->UpdateUserProfile(User);

	OnProfileUpdated.Broadcast();
	UE_LOG(LogD1, Log, TEXT("[Profile] 갱신 완료 score=%d level=%d"), User.Score, User.Level);
}

void UD1AuthSubsystem::SendHeartbeat()
{
	if (D1BackendHttp::GetSessionJwt(GetGameInstance()).IsEmpty())
	{
		return;
	}

	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(
		GetGameInstance(), TEXT("/api/auth/heartbeat"), D1BackendHttp::EBackendAuth::SessionJwt);
	D1BackendHttp::SendAsync(this, Request,
		[this](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			HandleHeartbeatResponse(Res, bSucceeded);
		});
}

void UD1AuthSubsystem::HandleHeartbeatResponse(const FHttpResponsePtr& Res, bool bSucceeded)
{
	// 네트워크 실패는 무시 — 세션 무효화가 아니라 일시 장애. 다음 주기에 재시도.
	if (!bSucceeded || !Res.IsValid())
	{
		return;
	}

	if (Res->GetResponseCode() == 200)
	{
		return;
	}

	// 401 등 — SESSION_SUPERSEDED만 공용 헬퍼가 처리(만료/기타 401은 이 기능 범위 밖).
	D1BackendHttp::HandleSupersededIfAny(GetGameInstance(), Res);
}
