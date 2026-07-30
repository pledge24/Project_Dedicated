// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1MatchmakingSubsystem.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
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
		Err.ErrorCode = EBackendErrorCode::Unknown;
		Err.ErrorMessage = TEXT("로그인이 필요합니다.");
		OnMatchmakingError.Broadcast(Err);
		return;
	}

	TMap<FString, FString> UpgradeHeaders;
	UpgradeHeaders.Add(TEXT("Authorization"), D1BackendHttp::MakeBearer(Jwt));

	const FString Url = BuildMatchWsUrl();
	MatchSocket = FWebSocketsModule::Get().CreateWebSocket(Url, TArray<FString>(), UpgradeHeaders);

	// CreateWebSocket은 스킴 미지원 URL(BaseUrl 오설정 등)에 null을 반환한다 — 바로 역참조하면
	// 매칭 버튼 한 번으로 클라가 죽는다. 상태를 되돌리고 에러로 표면화한다.
	if (!MatchSocket.IsValid())
	{
		FBackendResponse Err;
		Err.bOk = false;
		Err.ErrorCode = EBackendErrorCode::NetworkError;
		Err.ErrorMessage = TEXT("매칭 서버 주소가 올바르지 않습니다.");
		MatchmakingState = EMatchmakingState::Idle;
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
			(*DataObj)->TryGetStringField(TEXT("matchId"), Match.MatchId);
			(*DataObj)->TryGetStringField(TEXT("joinToken"), Match.JoinToken);

			const TSharedPtr<FJsonObject>* ServerObj = nullptr;
			if (D1BackendHttp::GetObjectField(*DataObj, TEXT("server"), ServerObj))
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
	Err.ErrorMessage = TEXT("");
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

	// 큐 대기/연결 중 예기치 않게 끊김 → 에러 표면화. 재연결은 이번 슬라이스 제외.
	CloseMatchSocket();
	MatchmakingState = EMatchmakingState::Idle;

	FBackendResponse Err;
	Err.bOk = false;
	Err.ErrorCode = EBackendErrorCode::NetworkError;
	Err.ErrorMessage = TEXT("매칭 서버 연결이 끊겼습니다.");
	OnMatchmakingError.Broadcast(Err);
}
