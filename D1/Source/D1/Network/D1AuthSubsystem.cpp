// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1AuthSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Framework/D1GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1BackendHttp.h"
#include "Network/D1SessionSubsystem.h"

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

void UD1AuthSubsystem::RefreshMyProfile()
{
	const FString Jwt = D1BackendHttp::GetSessionJwt(GetGameInstance());
	if (Jwt.IsEmpty())
	{
		// 비로그인 — 갱신할 세션 없음.
		return;
	}

	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(GetGameInstance(), TEXT("/api/auth/me"), /*bAttachAuth=*/true);

	TWeakObjectPtr<UD1AuthSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UD1AuthSubsystem* Self = WeakThis.Get())
			{
				Self->HandleProfileResponse(Req, Res, bSucceeded);
			}
		});
	Request->ProcessRequest();
}

void UD1AuthSubsystem::SendHeartbeat()
{
	const FString Jwt = D1BackendHttp::GetSessionJwt(GetGameInstance());
	if (Jwt.IsEmpty())
	{
		// 비로그인 — 확인할 세션 없음.
		return;
	}

	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(GetGameInstance(), TEXT("/api/auth/heartbeat"), /*bAttachAuth=*/true);

	TWeakObjectPtr<UD1AuthSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UD1AuthSubsystem* Self = WeakThis.Get())
			{
				Self->HandleHeartbeatResponse(Req, Res, bSucceeded);
			}
		});
	Request->ProcessRequest();
}

void UD1AuthSubsystem::SendAuthRequest(const FString& Path, const TSharedRef<FJsonObject>& Body, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildPostJson(GetGameInstance(), Path, Body, /*bAttachAuth=*/false);

	TWeakObjectPtr<UD1AuthSubsystem> WeakThis(this);
	const FOnAuthCompleted Forward = OnCompleted;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Forward](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UD1AuthSubsystem* Self = WeakThis.Get())
			{
				Self->HandleAuthResponse(Req, Res, bSucceeded, Forward);
			}
		});
	Request->ProcessRequest();
}

void UD1AuthSubsystem::HandleAuthResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded, FOnAuthCompleted Forward)
{
	FBackendResponse Out;
	FAuthUserDTO User;

	// fail case: 네트워크 자체 실패
	if (!bSucceeded || !Res.IsValid())
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::NetworkError;
		Out.ErrorMessage = TEXT("");
		UE_LOG(LogD1, Warning, TEXT("[Backend] HTTP 실패 (네트워크 또는 응답 없음)"));
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	const FString Content = Res->GetContentAsString();
	TSharedPtr<FJsonObject> Root;
	// fail case: content 역직렬화 실패
	if (!D1BackendHttp::DeserializeJson(Content, Root))
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		Out.ErrorMessage = TEXT("");
		UE_LOG(LogD1, Warning, TEXT("[Backend] JSON 역직렬화 실패: %s"), *Content);
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	const bool bOk = Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok"));
	if (bOk)
	{
		const TSharedPtr<FJsonObject>* DataObj = nullptr;
		if (D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
		{
			User.UserId   = static_cast<int32>((*DataObj)->GetNumberField(TEXT("userId")));
			User.Nickname = (*DataObj)->GetStringField(TEXT("nickname"));
			User.Score    = static_cast<int32>((*DataObj)->GetNumberField(TEXT("score")));

			// level/exp는 register/login 둘 다 응답에 포함. 미존재 시 기본값 유지.
			(*DataObj)->TryGetNumberField(TEXT("level"), User.Level);
			(*DataObj)->TryGetNumberField(TEXT("exp"),   User.Exp);

			// register 응답은 token이 없다 (서버가 가입 직후 자동 로그인을 막음).
			// token이 비어있으면 세션을 만들지 않고, 클라는 별도로 Login을 호출해야 한다.
			FString Token;
			(*DataObj)->TryGetStringField(TEXT("token"), Token);

			if (!Token.IsEmpty())
			{
				if (UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
				{
					GI->SetSession(Token, User);
				}
			}

			Out.bOk = true;
			Out.ErrorCode = EBackendErrorCode::None;
			Forward.ExecuteIfBound(Out, User);
			return;
		}

		// fail case: 성공 envelope + data 필드 가져오기 실패.
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		Out.ErrorMessage = TEXT("성공한 요청에 데이터를 가져올 수 없습니다");
		Forward.ExecuteIfBound(Out, User);
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
	Forward.ExecuteIfBound(Out, User);
}

void UD1AuthSubsystem::HandleProfileResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
{
	if (!bSucceeded || !Res.IsValid())
	{
		UE_LOG(LogD1, Warning, TEXT("[Profile] /me 갱신 실패 (네트워크)"));
		return;
	}

	// 옛 기기의 /me 요청도 401 SESSION_SUPERSEDED면 즉시 로그인 복귀.
	if (D1BackendHttp::HandleSupersededIfAny(GetGameInstance(), Res))
	{
		return;
	}

	const FString Content = Res->GetContentAsString();
	TSharedPtr<FJsonObject> Root;
	if (!D1BackendHttp::DeserializeJson(Content, Root)
		|| !(Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok"))))
	{
		UE_LOG(LogD1, Warning, TEXT("[Profile] /me 응답 파싱 실패: %s"), *Content);
		return;
	}

	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (!D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
	{
		return;
	}

	// login 응답과 동일 모양 — score/level/exp 최신값. token은 없음(세션은 그대로 유지).
	FAuthUserDTO User;
	User.UserId   = static_cast<int32>((*DataObj)->GetNumberField(TEXT("userId")));
	User.Nickname = (*DataObj)->GetStringField(TEXT("nickname"));
	User.Score    = static_cast<int32>((*DataObj)->GetNumberField(TEXT("score")));
	(*DataObj)->TryGetNumberField(TEXT("level"), User.Level);
	(*DataObj)->TryGetNumberField(TEXT("exp"),   User.Exp);

	if (UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
	{
		GI->UpdateUserProfile(User);
	}

	OnProfileUpdated.Broadcast();
	UE_LOG(LogD1, Log, TEXT("[Profile] 갱신 완료 score=%d level=%d"), User.Score, User.Level);
}

void UD1AuthSubsystem::HandleHeartbeatResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
{
	// 네트워크 실패는 무시 — 세션 무효화가 아니라 일시 장애. 다음 주기에 재시도.
	if (!bSucceeded || !Res.IsValid())
	{
		return;
	}

	if (Res->GetResponseCode() == 200)
	{
		return; // 세션 유효.
	}

	// 401 등 — SESSION_SUPERSEDED만 공용 헬퍼가 처리(만료/기타 401은 이 기능 범위 밖).
	D1BackendHttp::HandleSupersededIfAny(GetGameInstance(), Res);
}
