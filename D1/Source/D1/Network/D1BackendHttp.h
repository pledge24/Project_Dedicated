// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Network/BackendTypes.h"

class FJsonObject;
class UGameInstance;

/**
 *  백엔드 HTTP/JSON 공용 substrate. 상태 없는 자유함수 모음 —
 *  Auth/Matchmaking/Ranking/Result Subsystem이 공유(중복 제거). 세션 JWT는 GameInstance에서 읽는다.
 */
namespace D1BackendHttp
{
	/** 요청 인증 정책. */
	enum class EBackendAuth : uint8
	{
		/** 인증 헤더 없음 (로그인/가입). */
		None,
		/** GameInstance의 유저 세션 JWT. */
		SessionJwt,
		/** DS 매치별 서버 토큰 — Token 파라미터로 전달 (서버 권위 채널, 유저 JWT 아님). */
		ServerToken
	};

	/** 응답 envelope 공통 판정 결과. */
	enum class EEnvelopeOutcome : uint8
	{
		/** ok=true + data 확보. OutData 유효, OutResponse.bOk=true. */
		Ok,
		/** 실패 — OutResponse에 코드/메시지 채워짐. */
		Failed,
		/** SESSION_SUPERSEDED — SessionSubsystem이 화면 복귀를 처리. 호출자는 콜백 없이 종료. */
		Superseded
	};

	/** UD1OnlineSettings의 BaseUrl. */
	const FString& GetBaseUrl();

	/** "Bearer <token>". */
	FString MakeBearer(const FString& Token);

	/** GameInstance 세션 JWT(없으면 빈 문자열). */
	FString GetSessionJwt(const UGameInstance* GameInstance);

	/** POST + JSON 본문 요청 생성. Auth 정책에 따라 Authorization 첨부. */
	TSharedRef<IHttpRequest> BuildPostJson(const UGameInstance* GameInstance, const FString& Path,
		const TSharedRef<FJsonObject>& Body, EBackendAuth Auth, const FString& ServerToken = FString());

	/** GET 요청 생성(본문 없음). Auth 정책에 따라 Authorization 첨부. */
	TSharedRef<IHttpRequest> BuildGet(const UGameInstance* GameInstance, const FString& Path,
		EBackendAuth Auth, const FString& ServerToken = FString());

	/** 완료 콜백을 Owner 생존 시에만 실행하는 비동기 전송 (weak-guard 공통화). */
	void SendAsync(const UObject* Owner, const TSharedRef<IHttpRequest>& Request,
		TFunction<void(const FHttpResponsePtr&, bool)> OnComplete);

	/** 서버 응답 error.code 문자열 → enum. */
	EBackendErrorCode ParseErrorCode(const FString& CodeStr);

	/** 응답이 401 SESSION_SUPERSEDED면 SessionSubsystem에 통지(로그인 복귀). 처리했으면 true.
	 *  envelope 파싱이 없는 경로(heartbeat 등) 전용 — envelope 경로는 ParseEnvelope이 흡수. */
	bool HandleSupersededIfAny(const UGameInstance* GameInstance, const FHttpResponsePtr& Res);

	/**
	 *  응답 envelope 공통 판정 — 네트워크 실패/역직렬화 실패/세션 대체/실패 envelope/데이터 누락을
	 *  한 곳에서 처리한다. Ok면 OutData에 data 객체가 담긴다.
	 */
	EEnvelopeOutcome ParseEnvelope(const UGameInstance* GameInstance, const FHttpResponsePtr& Res,
		bool bSucceeded, FBackendResponse& OutResponse, TSharedPtr<FJsonObject>& OutData);

	//~ DTO 파싱 (서버 camelCase 키 계약을 한 곳에)
	/** data 객체 → 유저 프로필 (login/register/me 응답 공통 모양). */
	void ParseAuthUser(const TSharedPtr<FJsonObject>& Data, FAuthUserDTO& OutUser);
	/** data 객체 → 매치 성사 정보 (match:found 통지·재입장 응답 공통 모양). */
	void ParseMatchFound(const TSharedPtr<FJsonObject>& Data, FMatchFoundDTO& OutMatch);

	//~ JSON 헬퍼
	bool DeserializeJson(const FString& Content, TSharedPtr<FJsonObject>& OutRoot);
	bool GetObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const TSharedPtr<FJsonObject>*& Out);
	FString SerializeJson(const TSharedRef<FJsonObject>& Body);
}
