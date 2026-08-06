// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1BackendHttp.h"

#include "Core/D1LogChannels.h"
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
	namespace
	{
		void AttachAuth(const TSharedRef<IHttpRequest>& Request, const UGameInstance* GameInstance,
			EBackendAuth Auth, const FString& ServerToken)
		{
			switch (Auth)
			{
			case EBackendAuth::SessionJwt:
			{
				const FString Jwt = GetSessionJwt(GameInstance);
				if (!Jwt.IsEmpty())
				{
					Request->SetHeader(TEXT("Authorization"), MakeBearer(Jwt));
				}
				break;
			}
			case EBackendAuth::ServerToken:
				Request->SetHeader(TEXT("Authorization"), MakeBearer(ServerToken));
				break;
			case EBackendAuth::None:
			default:
				break;
			}
		}
	}

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
		const TSharedRef<FJsonObject>& Body, EBackendAuth Auth, const FString& ServerToken)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(GetBaseUrl() + Path);
		Request->SetVerb(TEXT("POST"));
		// 요청 전체 상한 — 엔진 기본 총 타임아웃은 0(비활성)이고, 기본 활동 타임아웃 30초는 '무응답'만 잡는다.
		Request->SetTimeout(GetDefault<UD1OnlineSettings>()->RequestTimeoutSec);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Request->SetContentAsString(SerializeJson(Body));
		AttachAuth(Request, GameInstance, Auth, ServerToken);

		return Request;
	}

	TSharedRef<IHttpRequest> BuildGet(const UGameInstance* GameInstance, const FString& Path,
		EBackendAuth Auth, const FString& ServerToken)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(GetBaseUrl() + Path);
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(GetDefault<UD1OnlineSettings>()->RequestTimeoutSec);
		AttachAuth(Request, GameInstance, Auth, ServerToken);

		return Request;
	}

	void SendAsync(const UObject* Owner, const TSharedRef<IHttpRequest>& Request,
		TFunction<void(const FHttpResponsePtr&, bool)> OnComplete)
	{
		TWeakObjectPtr<const UObject> WeakOwner(Owner);
		Request->OnProcessRequestComplete().BindLambda(
			[WeakOwner, OnComplete = MoveTemp(OnComplete)](FHttpRequestPtr, FHttpResponsePtr Res, bool bSucceeded)
			{
				if (WeakOwner.IsValid())
				{
					OnComplete(Res, bSucceeded);
				}
			});
		Request->ProcessRequest();
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

		if (UD1SessionSubsystem* Session = GameInstance->GetSubsystem<UD1SessionSubsystem>())
		{
			Session->NotifySessionSuperseded();

			return true;
		}

		return false;
	}

	EEnvelopeOutcome ParseEnvelope(const UGameInstance* GameInstance, const FHttpResponsePtr& Res,
		bool bSucceeded, FBackendResponse& OutResponse, TSharedPtr<FJsonObject>& OutData)
	{
		OutResponse.bOk = false;

		if (!bSucceeded || !Res.IsValid())
		{
			OutResponse.ErrorCode = EBackendErrorCode::NetworkError;
			UE_LOG(LogD1, Warning, TEXT("[Backend] HTTP 실패 (네트워크 또는 응답 없음)"));
			return EEnvelopeOutcome::Failed;
		}

		const FString Content = Res->GetContentAsString();
		TSharedPtr<FJsonObject> Root;
		if (!DeserializeJson(Content, Root))
		{
			OutResponse.ErrorCode = EBackendErrorCode::Unknown;
			UE_LOG(LogD1, Warning, TEXT("[Backend] JSON 역직렬화 실패: %s"), *Content);
			return EEnvelopeOutcome::Failed;
		}

		if (Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok")))
		{
			const TSharedPtr<FJsonObject>* DataObj = nullptr;
			if (GetObjectField(Root, TEXT("data"), DataObj))
			{
				OutData = *DataObj;
				OutResponse.bOk = true;
				OutResponse.ErrorCode = EBackendErrorCode::None;
				return EEnvelopeOutcome::Ok;
			}

			OutResponse.ErrorCode = EBackendErrorCode::Unknown;
			OutResponse.ErrorMessage = TEXT("성공한 요청에 데이터를 가져올 수 없습니다");
			return EEnvelopeOutcome::Failed;
		}

		const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
		if (GetObjectField(Root, TEXT("error"), ErrorObj))
		{
			FString CodeStr;
			(*ErrorObj)->TryGetStringField(TEXT("code"), CodeStr);
			const EBackendErrorCode Code = ParseErrorCode(CodeStr);

			// 세션 대체는 에러 콜백이 아니라 화면 복귀로 — 어느 인증 경로든 여기서 일괄 감지.
			if (Code == EBackendErrorCode::SessionSuperseded)
			{
				if (UD1SessionSubsystem* Session = GameInstance->GetSubsystem<UD1SessionSubsystem>())
				{
					Session->NotifySessionSuperseded();
					return EEnvelopeOutcome::Superseded;
				}
			}

			OutResponse.ErrorCode = Code;
			(*ErrorObj)->TryGetStringField(TEXT("message"), OutResponse.ErrorMessage);
			return EEnvelopeOutcome::Failed;
		}

		OutResponse.ErrorCode = EBackendErrorCode::Unknown;
		return EEnvelopeOutcome::Failed;
	}

	void ParseAuthUser(const TSharedPtr<FJsonObject>& Data, FAuthUserDTO& OutUser)
	{
		Data->TryGetNumberField(TEXT("userId"), OutUser.UserId);
		Data->TryGetStringField(TEXT("nickname"), OutUser.Nickname);
		Data->TryGetNumberField(TEXT("score"), OutUser.Score);
		// level/exp 미존재 시 기본값 유지.
		Data->TryGetNumberField(TEXT("level"), OutUser.Level);
		Data->TryGetNumberField(TEXT("exp"), OutUser.Exp);
	}

	void ParseMatchFound(const TSharedPtr<FJsonObject>& Data, FMatchFoundDTO& OutMatch)
	{
		Data->TryGetStringField(TEXT("matchId"), OutMatch.MatchId);
		Data->TryGetStringField(TEXT("joinToken"), OutMatch.JoinToken);

		const TSharedPtr<FJsonObject>* ServerObj = nullptr;
		if (GetObjectField(Data, TEXT("server"), ServerObj))
		{
			(*ServerObj)->TryGetStringField(TEXT("host"), OutMatch.ServerHost);
			(*ServerObj)->TryGetNumberField(TEXT("port"), OutMatch.ServerPort);
		}
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
