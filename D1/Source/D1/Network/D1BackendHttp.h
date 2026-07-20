// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Network/BackendTypes.h"

class FJsonObject;
class UGameInstance;

/**
 *  백엔드 HTTP/JSON 공용 substrate. 상태 없는 자유함수 모음 —
 *  Auth/Matchmaking/Result Subsystem이 공유(중복 제거). 세션 JWT는 GameInstance에서 읽는다.
 */
namespace D1BackendHttp
{
	/** UD1OnlineSettings의 BaseUrl. */
	const FString& GetBaseUrl();

	/** "Bearer <token>". */
	FString MakeBearer(const FString& Token);

	/** GameInstance 세션 JWT(없으면 빈 문자열). */
	FString GetSessionJwt(const UGameInstance* GameInstance);

	/** POST + JSON 본문 요청 생성. bAttachAuth면 세션 JWT를 Authorization으로 첨부. */
	TSharedRef<IHttpRequest> BuildPostJson(const UGameInstance* GameInstance, const FString& Path,
		const TSharedRef<FJsonObject>& Body, bool bAttachAuth);

	/** GET 요청 생성(본문 없음). bAttachAuth면 세션 JWT를 첨부. DS 폴링은 MatchToken을 직접 헤더로 세팅. */
	TSharedRef<IHttpRequest> BuildGet(const UGameInstance* GameInstance, const FString& Path, bool bAttachAuth);

	/** 서버 응답 error.code 문자열 → enum. */
	EBackendErrorCode ParseErrorCode(const FString& CodeStr);

	/** 응답이 401 SESSION_SUPERSEDED면 SessionSubsystem에 통지(로그인 복귀). 처리했으면 true.
	 *  모든 인증 요청 응답 핸들러가 초입에 호출 → 옛 기기의 어떤 요청이든 즉시 팝업. */
	bool HandleSupersededIfAny(const UGameInstance* GameInstance, const FHttpResponsePtr& Res);

	//~ JSON 헬퍼
	bool DeserializeJson(const FString& Content, TSharedPtr<FJsonObject>& OutRoot);
	bool GetObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const TSharedPtr<FJsonObject>*& Out);
	FString SerializeJson(const TSharedRef<FJsonObject>& Body);
}
