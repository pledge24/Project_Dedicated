// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/BackendSubsystem.h"

#include "D1.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Online/D1GameInstance.h"
#include "Online/D1OnlineSettings.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogD1, Log, TEXT("[Backend] Subsystem 초기화 (BaseUrl=%s)"), *GetBaseUrl());
}

void UBackendSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

const FString& UBackendSubsystem::GetBaseUrl() const
{
	return GetDefault<UD1OnlineSettings>()->BaseUrl;
}

void UBackendSubsystem::Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("nickname"), Nickname);

	const TSharedRef<IHttpRequest> Request = BuildPostJson(TEXT("/api/auth/register"), Body, /*bAttachAuth=*/false);

	TWeakObjectPtr<UBackendSubsystem> WeakThis(this);
	const FOnAuthCompleted Forward = OnCompleted;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Forward](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded)
		{
			if (UBackendSubsystem* Self = WeakThis.Get())
			{
				Self->HandleAuthResponse(Req, Resp, bSucceeded, Forward);
			}
		});
	Request->ProcessRequest();
}

void UBackendSubsystem::Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);

	const TSharedRef<IHttpRequest> Request = BuildPostJson(TEXT("/api/auth/login"), Body, /*bAttachAuth=*/false);

	TWeakObjectPtr<UBackendSubsystem> WeakThis(this);
	const FOnAuthCompleted Forward = OnCompleted;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Forward](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded)
		{
			if (UBackendSubsystem* Self = WeakThis.Get())
			{
				Self->HandleAuthResponse(Req, Resp, bSucceeded, Forward);
			}
		});
	Request->ProcessRequest();
}

TSharedRef<IHttpRequest> UBackendSubsystem::BuildPostJson(const FString& Path, const TSharedRef<FJsonObject>& Body, bool bAttachAuth) const
{
	FString Serialized;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
		= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(Body, Writer);

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GetBaseUrl() + Path);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetContentAsString(Serialized);

	if (bAttachAuth)
	{
		if (const UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
		{
			const FString& Jwt = GI->GetCurrentJwt();
			if (!Jwt.IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Jwt));
			}
		}
	}

	return Request;
}

void UBackendSubsystem::HandleAuthResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded, FOnAuthCompleted Forward)
{
	FBackendResponse Out;
	FAuthUserDTO User;

	// 네트워크 자체 실패
	if (!bSucceeded || !Resp.IsValid())
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::NetworkError;
		Out.ErrorMessage = TEXT("");
		UE_LOG(LogD1, Warning, TEXT("[Backend] HTTP 실패 (네트워크 또는 응답 없음)"));
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	const FString Content = Resp->GetContentAsString();
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		Out.ErrorMessage = TEXT("");
		UE_LOG(LogD1, Warning, TEXT("[Backend] JSON 파싱 실패: %s"), *Content);
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	const bool bOk = Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok"));
	if (bOk)
	{
		const TSharedPtr<FJsonObject>* DataObj = nullptr;
		if (Root->TryGetObjectField(TEXT("data"), DataObj) && DataObj && DataObj->IsValid())
		{
			User.UserId   = static_cast<int32>((*DataObj)->GetNumberField(TEXT("userId")));
			User.Nickname = (*DataObj)->GetStringField(TEXT("nickname"));
			User.Score    = static_cast<int32>((*DataObj)->GetNumberField(TEXT("score")));

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

		Out.bOk = false;
		Out.ErrorCode = EBackendErrorCode::Unknown;
		Out.ErrorMessage = TEXT("");
		Forward.ExecuteIfBound(Out, User);
		return;
	}

	// 실패 envelope
	const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
	if (Root->TryGetObjectField(TEXT("error"), ErrorObj) && ErrorObj && ErrorObj->IsValid())
	{
		const FString CodeStr = (*ErrorObj)->GetStringField(TEXT("code"));
		Out.ErrorCode    = ParseErrorCode(CodeStr);
		Out.ErrorMessage = (*ErrorObj)->GetStringField(TEXT("message"));
	}
	else
	{
		Out.ErrorCode = EBackendErrorCode::Unknown;
	}
	Out.bOk = false;
	Forward.ExecuteIfBound(Out, User);
}

EBackendErrorCode UBackendSubsystem::ParseErrorCode(const FString& CodeStr)
{
	if (CodeStr == TEXT("VALIDATION_FAILED"))    return EBackendErrorCode::ValidationFailed;
	if (CodeStr == TEXT("INVALID_CREDENTIALS"))  return EBackendErrorCode::InvalidCredentials;
	if (CodeStr == TEXT("DUPLICATE_LOGIN_ID"))   return EBackendErrorCode::DuplicateLoginId;
	if (CodeStr == TEXT("DUPLICATE_NICKNAME"))   return EBackendErrorCode::DuplicateNickname;
	if (CodeStr == TEXT("RATE_LIMITED"))         return EBackendErrorCode::RateLimited;
	if (CodeStr == TEXT("INTERNAL_ERROR"))       return EBackendErrorCode::InternalError;
	return EBackendErrorCode::Unknown;
}
