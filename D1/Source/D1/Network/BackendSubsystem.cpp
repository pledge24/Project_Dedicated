// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/BackendSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Framework/D1GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Network/D1OnlineSettings.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebSocketsModule.h"

void UBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogD1, Log, TEXT("[Backend] Subsystem 초기화 (BaseUrl=%s)"), *GetBaseUrl());
}

void UBackendSubsystem::Deinitialize()
{
	CloseMatchSocket();
	Super::Deinitialize();
}

void UBackendSubsystem::Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("nickname"), Nickname);

	SendAuthRequest(TEXT("/api/auth/register"), Body, OnCompleted);
}

void UBackendSubsystem::Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("loginId"),  LoginId);
	Body->SetStringField(TEXT("password"), Password);

	SendAuthRequest(TEXT("/api/auth/login"), Body, OnCompleted);
}

void UBackendSubsystem::StartMatchmaking()
{
	// 직전 매치에서 Matched로 남은 잔류 상태 — Subsystem이 travel을 가로질러 살아남아
	// 로비 복귀 후에도 유지됨. 다시 매칭 = 새 매칭이므로 Idle로 리셋(소켓은 입장 직전 이미 닫힘).
	if (MatchmakingState == EMatchmakingState::Matched)
	{
		CloseMatchSocket();
		MatchmakingState = EMatchmakingState::Idle;
	}

	if (MatchmakingState != EMatchmakingState::Idle)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 이미 매칭 중 (state=%d) — Start 무시"), static_cast<int32>(MatchmakingState));
		return;
	}

	const UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance());
	const FString Jwt = GI ? GI->GetCurrentJwt() : FString();
	if (Jwt.IsEmpty())
	{
		FBackendResponse Err;
		Err.bOk = false;
		Err.ErrorCode = EBackendErrorCode::Unknown;
		Err.ErrorMessage = TEXT("로그인이 필요합니다.");
		OnMatchmakingError.Broadcast(Err);
		return;
	}

	TMap<FString, FString> UpgradeHeaders;
	UpgradeHeaders.Add(TEXT("Authorization"), MakeBearer(Jwt));

	const FString Url = BuildMatchWsUrl();
	MatchSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TArray<FString>(), UpgradeHeaders);

	// Connect() 전에 바인딩 (엔진 StompClient 관용구). UObject라 AddUObject 사용.
	MatchSocket->OnConnected().AddUObject(this, &UBackendSubsystem::HandleSocketConnected);
	MatchSocket->OnConnectionError().AddUObject(this, &UBackendSubsystem::HandleSocketConnectionError);
	MatchSocket->OnClosed().AddUObject(this, &UBackendSubsystem::HandleSocketClosed);
	MatchSocket->OnMessage().AddUObject(this, &UBackendSubsystem::HandleSocketMessage);

	MatchmakingState = EMatchmakingState::Connecting;
	UE_LOG(LogD1, Log, TEXT("[Match] WS 연결 시도: %s"), *Url);
	MatchSocket->Connect();
}

void UBackendSubsystem::CancelMatchmaking()
{
	// 4가지 상태 중 유일하게 큐 밖에 있는 Idle 상태만 거른다.
	if (MatchmakingState == EMatchmakingState::Idle)
	{
		CloseMatchSocket();	// 혹시나 ws 소켓이 열려있는 경우
		return;
	}

	if (MatchSocket.IsValid() && MatchSocket->IsConnected())
	{
		SendType(TEXT("queue:cancel"));
	}
	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;
	UE_LOG(LogD1, Log, TEXT("[Match] 매칭 취소"));
}

void UBackendSubsystem::RefreshMyProfile()
{
	const UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance());
	const FString Jwt = GI ? GI->GetCurrentJwt() : FString();
	if (Jwt.IsEmpty())
	{
		// 비로그인 — 갱신할 세션 없음.
		return;
	}

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GetBaseUrl() + TEXT("/api/auth/me"));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Authorization"), MakeBearer(Jwt));

	TWeakObjectPtr<UBackendSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UBackendSubsystem* Self = WeakThis.Get())
			{
				Self->HandleProfileResponse(Req, Res, bSucceeded);
			}
		});
	Request->ProcessRequest();
}

const FString& UBackendSubsystem::GetBaseUrl() const
{
	return GetDefault<UD1OnlineSettings>()->BaseUrl;
}

void UBackendSubsystem::ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
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
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Body->SetArrayField(TEXT("results"), Results);

	// 매치별 서버 토큰을 Bearer로 — 유저 JWT가 아니라 DS 인증 채널. (BuildPostJson은 auth 미첨부로 호출.)
	const TSharedRef<IHttpRequest> Request = BuildPostJson(TEXT("/api/match/result"), Body, /*bAttachAuth=*/false);
	Request->SetHeader(TEXT("Authorization"), MakeBearer(MatchToken));

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

void UBackendSubsystem::SendAuthRequest(const FString& Path, const TSharedRef<FJsonObject>& Body, const FOnAuthCompleted& OnCompleted)
{
	const TSharedRef<IHttpRequest> Request = BuildPostJson(Path, Body, /*bAttachAuth=*/false);

	TWeakObjectPtr<UBackendSubsystem> WeakThis(this);
	const FOnAuthCompleted Forward = OnCompleted;
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Forward](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
		{
			if (UBackendSubsystem* Self = WeakThis.Get())
			{
				Self->HandleAuthResponse(Req, Res, bSucceeded, Forward);
			}
		});
	Request->ProcessRequest();
}

TSharedRef<IHttpRequest> UBackendSubsystem::BuildPostJson(const FString& Path, const TSharedRef<FJsonObject>& Body, bool bAttachAuth) const
{
	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GetBaseUrl() + Path);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetContentAsString(SerializeJson(Body));

	if (bAttachAuth)
	{
		if (const UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
		{
			const FString& Jwt = GI->GetCurrentJwt();
			if (!Jwt.IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), MakeBearer(Jwt));
			}
		}
	}

	return Request;
}

void UBackendSubsystem::HandleAuthResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded, FOnAuthCompleted Forward)
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
	if (!DeserializeJson(Content, Root))
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
		if (GetObjectField(Root, TEXT("data"), DataObj))
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
	if (GetObjectField(Root, TEXT("error"), ErrorObj))
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

void UBackendSubsystem::HandleProfileResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSucceeded)
{
	if (!bSucceeded || !Res.IsValid())
	{
		UE_LOG(LogD1, Warning, TEXT("[Profile] /me 갱신 실패 (네트워크)"));
		return;
	}

	const FString Content = Res->GetContentAsString();
	TSharedPtr<FJsonObject> Root;
	if (!DeserializeJson(Content, Root)
		|| !(Root->HasField(TEXT("ok")) && Root->GetBoolField(TEXT("ok"))))
	{
		UE_LOG(LogD1, Warning, TEXT("[Profile] /me 응답 파싱 실패: %s"), *Content);
		return;
	}

	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (!GetObjectField(Root, TEXT("data"), DataObj))
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

bool UBackendSubsystem::DeserializeJson(const FString& Content, TSharedPtr<FJsonObject>& OutRoot)
{
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
	return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
}

bool UBackendSubsystem::GetObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const TSharedPtr<FJsonObject>*& Out)
{
	return Obj.IsValid() && Obj->TryGetObjectField(Field, Out) && Out && Out->IsValid();
}

FString UBackendSubsystem::SerializeJson(const TSharedRef<FJsonObject>& Body)
{
	FString Serialized;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
		= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(Body, Writer);
	return Serialized;
}

FString UBackendSubsystem::MakeBearer(const FString& Token)
{
	return FString::Printf(TEXT("Bearer %s"), *Token);
}

FString UBackendSubsystem::BuildMatchWsUrl() const
{
	FString Url = GetBaseUrl();
	if (Url.StartsWith(TEXT("https://")))
	{
		Url = TEXT("wss://") + Url.RightChop(8);
	}
	else if (Url.StartsWith(TEXT("http://")))
	{
		Url = TEXT("ws://") + Url.RightChop(7);
	}
	return Url + TEXT("/ws/match");
}

void UBackendSubsystem::SendType(const FString& Type)
{
	if (!MatchSocket.IsValid() || !MatchSocket->IsConnected())
	{
		return;
	}

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("type"), Type);

	MatchSocket->Send(SerializeJson(Body));
}

void UBackendSubsystem::CloseMatchSocket()
{
	if (!MatchSocket.IsValid())
	{
		return;
	}

	MatchSocket->OnConnected().RemoveAll(this);
	MatchSocket->OnConnectionError().RemoveAll(this);
	MatchSocket->OnClosed().RemoveAll(this);
	MatchSocket->OnMessage().RemoveAll(this);
	if (MatchSocket->IsConnected())
	{
		MatchSocket->Close();
	}
	MatchSocket.Reset();
}

void UBackendSubsystem::HandleSocketConnected()
{
	UE_LOG(LogD1, Log, TEXT("[Match] WS 연결됨 — queue:join 전송"));
	SendType(TEXT("queue:join"));
}

void UBackendSubsystem::HandleSocketMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Root;
	if (!DeserializeJson(Message, Root))
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] WS 메시지 파싱 실패: %s"), *Message);
		return;
	}

	const FString Type = Root->GetStringField(TEXT("type"));

	if (Type == TEXT("queue:joined"))
	{
		MatchmakingState = EMatchmakingState::Queued;
		OnQueueJoined.Broadcast();
		return;
	}

	if (Type == TEXT("queue:left"))
	{
		MatchmakingState = EMatchmakingState::Idle;
		return;
	}

	if (Type == TEXT("match:found"))
	{
		FMatchFoundDTO Match;
		const TSharedPtr<FJsonObject>* DataObj = nullptr;
		if (GetObjectField(Root, TEXT("data"), DataObj))
		{
			(*DataObj)->TryGetStringField(TEXT("matchId"), Match.MatchId);
			(*DataObj)->TryGetStringField(TEXT("joinToken"), Match.JoinToken);

			const TSharedPtr<FJsonObject>* ServerObj = nullptr;
			if (GetObjectField(*DataObj, TEXT("server"), ServerObj))
			{
				(*ServerObj)->TryGetStringField(TEXT("host"), Match.ServerHost);
				(*ServerObj)->TryGetNumberField(TEXT("port"), Match.ServerPort);
			}
		}

		MatchmakingState = EMatchmakingState::Matched;
		UE_LOG(LogD1, Log, TEXT("[Match] 매칭 성사 matchId=%s server=%s:%d"),
			*Match.MatchId, *Match.ServerHost, Match.ServerPort);
		OnMatchFound.Broadcast(Match);

		// travel이 월드를 내리므로 WS 먼저 정리.
		CloseMatchSocket();

		// 할당받은 DS 주소로 입장. 로컬 PC에서 raw "host:port"로 ClientTravel.
		if (!Match.ServerHost.IsEmpty() && Match.ServerPort > 0)
		{
			if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
			{
				// 본인 입장 토큰을 ?join= 으로 동봉 → DS가 roster로 권위 신원(userId·slot) 확정.
				// userId/slot은 클라가 주장하지 않는다(서버권위). UE URL 옵션은 ? 로 구분.
				if (Match.JoinToken.IsEmpty())
				{
					UE_LOG(LogD1, Warning, TEXT("[Match] match:found에 join 토큰 없음 — 신원 매핑 실패 가능"));
				}

				const FString Url = FString::Printf(TEXT("%s:%d?matchId=%s?join=%s"),
					*Match.ServerHost, Match.ServerPort, *Match.MatchId, *Match.JoinToken);
				UE_LOG(LogD1, Log, TEXT("[Match] DS 입장: %s"), *Url);
				PC->ClientTravel(Url, TRAVEL_Absolute);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] 로컬 PlayerController 없음 — travel 불가"));
			}
		}
		return;
	}

	if (Type == TEXT("error"))
	{
		FBackendResponse Err;
		Err.bOk = false;
		const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
		if (GetObjectField(Root, TEXT("error"), ErrorObj))
		{
			Err.ErrorCode = ParseErrorCode((*ErrorObj)->GetStringField(TEXT("code")));
			(*ErrorObj)->TryGetStringField(TEXT("message"), Err.ErrorMessage);
		}
		else
		{
			Err.ErrorCode = EBackendErrorCode::Unknown;
		}
		UE_LOG(LogD1, Warning, TEXT("[Match] 서버 에러: %s"), *Err.ErrorMessage);
		OnMatchmakingError.Broadcast(Err);
		return;
	}

	UE_LOG(LogD1, Warning, TEXT("[Match] 알 수 없는 WS 메시지 type=%s"), *Type);
}

void UBackendSubsystem::HandleSocketConnectionError(const FString& Error)
{
	UE_LOG(LogD1, Warning, TEXT("[Match] WS 연결 에러: %s"), *Error);

	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::NetworkError;
	Err.ErrorMessage = TEXT("");
	OnMatchmakingError.Broadcast(Err);
}

void UBackendSubsystem::HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogD1, Log, TEXT("[Match] WS 종료 code=%d clean=%d reason=%s"), StatusCode, bWasClean ? 1 : 0, *Reason);

	// 매칭 성사 후 우리가 닫았거나(Matched), 취소(Idle)면 정상 — 에러 아님.
	if (MatchmakingState == EMatchmakingState::Matched || MatchmakingState == EMatchmakingState::Idle)
	{
		return;
	}

	// 큐 대기/연결 중 예기치 않게 끊김 → 에러 표면화. 재연결은 이번 슬라이스 제외.
	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::NetworkError;
	Err.ErrorMessage = TEXT("매칭 서버 연결이 끊겼습니다.");
	OnMatchmakingError.Broadcast(Err);
}
