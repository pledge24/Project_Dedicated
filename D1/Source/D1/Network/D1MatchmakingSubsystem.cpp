// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1MatchmakingSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Network/D1BackendHttp.h"
#include "Network/D1SessionSubsystem.h"
#include "WebSocketsModule.h"

void UD1MatchmakingSubsystem::Deinitialize()
{
	CloseMatchSocket();
	Super::Deinitialize();
}

void UD1MatchmakingSubsystem::StartMatchmaking()
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

	const FString Jwt = D1BackendHttp::GetSessionJwt(GetGameInstance());
	if (Jwt.IsEmpty())
	{
		FBackendResponse Err;
		Err.bOk = false;
		Err.ErrorCode = EBackendErrorCode::NotAuthenticated;
		OnMatchmakingError.Broadcast(Err);
		return;
	}

	TMap<FString, FString> UpgradeHeaders;
	UpgradeHeaders.Add(TEXT("Authorization"), D1BackendHttp::MakeBearer(Jwt));

	const FString Url = BuildMatchWsUrl();
	MatchSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TArray<FString>(), UpgradeHeaders);

	// CreateWebSocket은 스킴 미지원 URL(BaseUrl 오설정 등)에 null을 반환한다 — 바로 역참조하면
	// 매칭 버튼 한 번으로 클라가 죽는다. 에러로 표면화한다(상태는 아직 Idle).
	if (!MatchSocket.IsValid())
	{
		FBackendResponse Err;
		Err.bOk = false;
		Err.ErrorCode = EBackendErrorCode::NetworkError;
		Err.ErrorMessage = TEXT("매칭 서버 주소가 올바르지 않습니다.");
		UE_LOG(LogD1, Error, TEXT("[Match] WS 생성 실패 — URL 확인 필요: %s"), *Url);
		OnMatchmakingError.Broadcast(Err);

		return;
	}

	// Connect() 전에 바인딩 (엔진 StompClient 관용구). UObject라 AddUObject 사용.
	MatchSocket->OnConnected().AddUObject(this, &UD1MatchmakingSubsystem::HandleSocketConnected);
	MatchSocket->OnConnectionError().AddUObject(this, &UD1MatchmakingSubsystem::HandleSocketConnectionError);
	MatchSocket->OnClosed().AddUObject(this, &UD1MatchmakingSubsystem::HandleSocketClosed);
	MatchSocket->OnMessage().AddUObject(this, &UD1MatchmakingSubsystem::HandleSocketMessage);

	MatchmakingState = EMatchmakingState::Connecting;
	UE_LOG(LogD1, Log, TEXT("[Match] WS 연결 시도: %s"), *Url);
	MatchSocket->Connect();
}

void UD1MatchmakingSubsystem::CancelMatchmaking()
{
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

FString UD1MatchmakingSubsystem::BuildMatchWsUrl() const
{
	FString Url = D1BackendHttp::GetBaseUrl();
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

void UD1MatchmakingSubsystem::SendType(const FString& Type)
{
	if (!MatchSocket.IsValid() || !MatchSocket->IsConnected())
	{
		return;
	}

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("type"), Type);

	MatchSocket->Send(D1BackendHttp::SerializeJson(Body));
}

void UD1MatchmakingSubsystem::CloseMatchSocket()
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

void UD1MatchmakingSubsystem::HandleSocketConnected()
{
	UE_LOG(LogD1, Log, TEXT("[Match] WS 연결됨 — queue:join 전송"));
	SendType(TEXT("queue:join"));
}

void UD1MatchmakingSubsystem::HandleSocketMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Root;
	if (!D1BackendHttp::DeserializeJson(Message, Root))
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] WS 메시지 파싱 실패: %s"), *Message);
		return;
	}

	const FString Type = Root->GetStringField(TEXT("type"));

	if (Type == TEXT("session:invalid"))
	{
		// 다른 기기 로그인으로 세션 대체 — 곧 서버가 close(4001). Idle로 만들어 뒤이은 close를 정상 종료로 흡수
		// (HandleSocketClosed가 NetworkError로 오탐하지 않게). 실제 화면 복귀는 SessionSubsystem이 담당(멱등).
		MatchmakingState = EMatchmakingState::Idle;
		if (UD1SessionSubsystem* Session = GetGameInstance()->GetSubsystem<UD1SessionSubsystem>())
		{
			Session->NotifySessionSuperseded();
		}
		return;
	}

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
		if (D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
		{
			D1BackendHttp::ParseMatchFound(*DataObj, Match);
		}

		MatchmakingState = EMatchmakingState::Matched;
		UE_LOG(LogD1, Log, TEXT("[Match] 매칭 성사 matchId=%s server=%s:%d"),
			*Match.MatchId, *Match.ServerHost, Match.ServerPort);
		OnMatchFound.Broadcast(Match);

		// travel이 월드를 내리므로 WS 먼저 정리.
		CloseMatchSocket();
		TravelToMatch(Match);
		return;
	}

	if (Type == TEXT("error"))
	{
		FBackendResponse Err;
		Err.bOk = false;
		const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
		if (D1BackendHttp::GetObjectField(Root, TEXT("error"), ErrorObj))
		{
			Err.ErrorCode = D1BackendHttp::ParseErrorCode((*ErrorObj)->GetStringField(TEXT("code")));
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

void UD1MatchmakingSubsystem::HandleSocketConnectionError(const FString& Error)
{
	UE_LOG(LogD1, Warning, TEXT("[Match] WS 연결 에러: %s"), *Error);

	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::NetworkError;
	OnMatchmakingError.Broadcast(Err);
}

void UD1MatchmakingSubsystem::HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogD1, Log, TEXT("[Match] WS 종료 code=%d clean=%d reason=%s"), StatusCode, bWasClean ? 1 : 0, *Reason);

	// 매칭 성사 후 우리가 닫았거나(Matched), 취소(Idle)면 정상 — 에러 아님.
	if (MatchmakingState == EMatchmakingState::Matched || MatchmakingState == EMatchmakingState::Idle)
	{
		return;
	}

	// 큐 대기/연결 중 예기치 않게 끊김 → 에러 표면화(자동 재연결 없음).
	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::NetworkError;
	Err.ErrorMessage = TEXT("매칭 서버 연결이 끊겼습니다.");
	OnMatchmakingError.Broadcast(Err);
}

void UD1MatchmakingSubsystem::CheckRejoinableMatch()
{
	// 매칭 중이면 확인하지 않는다 — 큐/확정 흐름이 진행 중인데 travel을 끼워 넣으면 상태가 어긋난다.
	if (MatchmakingState != EMatchmakingState::Idle)
	{
		return;
	}

	if (D1BackendHttp::GetSessionJwt(GetGameInstance()).IsEmpty())
	{
		return;
	}

	const TSharedRef<IHttpRequest> Request = D1BackendHttp::BuildGet(
		GetGameInstance(), TEXT("/api/match/current"), D1BackendHttp::EBackendAuth::SessionJwt);
	D1BackendHttp::SendAsync(this, Request,
		[this](const FHttpResponsePtr& Res, bool bSucceeded)
		{
			// 로비 진입 직후는 첫 heartbeat 전이라 이 요청이 세션 대체를 감지할 유일한 창이다.
			if (D1BackendHttp::HandleSupersededIfAny(GetGameInstance(), Res))
			{
				return;
			}

			// 실패는 조용히 무시한다 — 재입장은 있으면 좋은 복구 경로지 로비 진입을 막을 이유가 아니다.
			if (!bSucceeded || !Res.IsValid() || Res->GetResponseCode() != 200)
			{
				return;
			}

			HandleRejoinResponse(Res->GetContentAsString());
		});
}

void UD1MatchmakingSubsystem::HandleRejoinResponse(const FString& Body)
{
	TSharedPtr<FJsonObject> Root;
	if (!D1BackendHttp::DeserializeJson(Body, Root))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* DataObj = nullptr;
	if (!D1BackendHttp::GetObjectField(Root, TEXT("data"), DataObj))
	{
		return;
	}

	bool bActive = false;
	if (!(*DataObj)->TryGetBoolField(TEXT("active"), bActive) || !bActive)
	{
		return;
	}

	FMatchFoundDTO Match;
	D1BackendHttp::ParseMatchFound(*DataObj, Match);

	// 성사 경로와 같은 상태·이벤트를 태운다 — 로비 UI가 이미 이 흐름을 구독하고 있다.
	MatchmakingState = EMatchmakingState::Matched;
	UE_LOG(LogD1, Log, TEXT("[Match] 진행 중 매치로 재입장 matchId=%s server=%s:%d"),
		*Match.MatchId, *Match.ServerHost, Match.ServerPort);
	OnMatchFound.Broadcast(Match);

	TravelToMatch(Match);
}

void UD1MatchmakingSubsystem::TravelToMatch(const FMatchFoundDTO& Match)
{
	if (Match.ServerHost.IsEmpty() || Match.ServerPort <= 0)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 매치 서버 주소 불량 host=%s port=%d — travel 불가"),
			*Match.ServerHost, Match.ServerPort);
		NotifyTravelFailed(TEXT("매치 서버 주소가 올바르지 않습니다."));
		return;
	}

	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (!PC)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 로컬 PlayerController 없음 — travel 불가"));
		NotifyTravelFailed(TEXT("매치 입장에 실패했습니다."));
		return;
	}

	// 본인 입장 토큰을 ?join= 으로 동봉 → DS가 roster로 권위 신원(userId·slot) 확정.
	// userId/slot은 클라가 주장하지 않는다(서버권위). UE URL 옵션은 ? 로 구분.
	if (Match.JoinToken.IsEmpty())
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] join 토큰 없음 — 신원 매핑 실패 가능"));
	}

	const FString Url = FString::Printf(TEXT("%s:%d?matchId=%s?join=%s"),
		*Match.ServerHost, Match.ServerPort, *Match.MatchId, *Match.JoinToken);
	UE_LOG(LogD1, Log, TEXT("[Match] DS 입장: %s"), *Url);
	PC->ClientTravel(Url, TRAVEL_Absolute);
}

void UD1MatchmakingSubsystem::NotifyTravelFailed(const FString& Message)
{
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::Unknown;
	Err.ErrorMessage = Message;
	OnMatchmakingError.Broadcast(Err);
}
