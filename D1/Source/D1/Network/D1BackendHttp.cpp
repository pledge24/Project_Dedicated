// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1BackendHttp.h"

#include "Dom/JsonObject.h"
#include "Framework/D1GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Network/D1OnlineSettings.h"
#include "Network/D1SessionSubsystem.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace D1BackendHttp
{
	const FString& GetBaseUrl()
	{
		return GetDefault<UD1OnlineSettings>()->BaseUrl;
	}

	FString MakeBearer(const FString& Token)
	{
		return FString::Printf(TEXT("Bearer %s"), *Token);
	}

	FString GetSessionJwt(const UGameInstance* GameInstance)
	{
		const UD1GameInstance* GI = Cast<UD1GameInstance>(GameInstance);
		return GI ? GI->GetCurrentJwt() : FString();
	}

	TSharedRef<IHttpRequest> BuildPostJson(const UGameInstance* GameInstance, const FString& Path,
		const TSharedRef<FJsonObject>& Body, bool bAttachAuth)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(GetBaseUrl() + Path);
		Request->SetVerb(TEXT("POST"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Request->SetContentAsString(SerializeJson(Body));

		if (bAttachAuth)
		{
			const FString Jwt = GetSessionJwt(GameInstance);
			if (!Jwt.IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), MakeBearer(Jwt));
			}
		}

		return Request;
	}

	TSharedRef<IHttpRequest> BuildGet(const UGameInstance* GameInstance, const FString& Path, bool bAttachAuth)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(GetBaseUrl() + Path);
		Request->SetVerb(TEXT("GET"));

		if (bAttachAuth)
		{
			const FString Jwt = GetSessionJwt(GameInstance);
			if (!Jwt.IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), MakeBearer(Jwt));
			}
		}

		return Request;
	}

	EBackendErrorCode ParseErrorCode(const FString& CodeStr)
	{
		if (CodeStr == TEXT("VALIDATION_FAILED"))    return EBackendErrorCode::ValidationFailed;
		if (CodeStr == TEXT("INVALID_CREDENTIALS"))  return EBackendErrorCode::InvalidCredentials;
		if (CodeStr == TEXT("DUPLICATE_LOGIN_ID"))   return EBackendErrorCode::DuplicateLoginId;
		if (CodeStr == TEXT("DUPLICATE_NICKNAME"))   return EBackendErrorCode::DuplicateNickname;
		if (CodeStr == TEXT("RATE_LIMITED"))         return EBackendErrorCode::RateLimited;
		if (CodeStr == TEXT("SESSION_SUPERSEDED"))   return EBackendErrorCode::SessionSuperseded;
		if (CodeStr == TEXT("INTERNAL_ERROR"))       return EBackendErrorCode::InternalError;
		return EBackendErrorCode::Unknown;
	}

	bool HandleSupersededIfAny(const UGameInstance* GameInstance, const FHttpResponsePtr& Res)
	{
		if (!Res.IsValid())
		{
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		if (!DeserializeJson(Res->GetContentAsString(), Root))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
		if (!GetObjectField(Root, TEXT("error"), ErrorObj))
		{
			return false;
		}
		if (ParseErrorCode((*ErrorObj)->GetStringField(TEXT("code"))) != EBackendErrorCode::SessionSuperseded)
		{
			return false;
		}

		if (GameInstance)
		{
			if (UD1SessionSubsystem* Session = GameInstance->GetSubsystem<UD1SessionSubsystem>())
			{
				Session->NotifySessionSuperseded();

				return true;
			}
		}

		return false;
	}

	bool DeserializeJson(const FString& Content, TSharedPtr<FJsonObject>& OutRoot)
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	bool GetObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const TSharedPtr<FJsonObject>*& Out)
	{
		return Obj.IsValid() && Obj->TryGetObjectField(Field, Out) && Out && Out->IsValid();
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Body)
	{
		FString Serialized;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
			= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
		FJsonSerializer::Serialize(Body, Writer);
		return Serialized;
	}
}
