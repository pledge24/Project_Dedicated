// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BackendTypes.generated.h"

/** 백엔드 에러 코드. 서버 응답 envelope의 error.code를 매핑한 enum. */
UENUM(BlueprintType)
enum class EBackendErrorCode : uint8
{
	None,
	ValidationFailed,
	InvalidCredentials,
	DuplicateLoginId,
	DuplicateNickname,
	RateLimited,
	NetworkError,        // HTTP 자체 실패 (서버 다운 / DNS / 타임아웃)
	InternalError,
	Unknown
};

/** 인증된 유저 정보 (토큰 제외 — 토큰은 GameInstance가 별도 보관). */
USTRUCT(BlueprintType)
struct FAuthUserDTO
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 UserId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Score = 1000;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 Exp = 0;
};

/** 백엔드 응답 결과. 성공 여부 + 실패 시 코드/메시지. */
USTRUCT(BlueprintType)
struct FBackendResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	bool bOk = false;

	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	EBackendErrorCode ErrorCode = EBackendErrorCode::None;

	/** 서버가 내려준 사용자 메시지 (한글). 비어있으면 클라가 fallback. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString ErrorMessage;
};

/** 회원가입/로그인 완료 콜백. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAuthCompleted, const FBackendResponse&, Response, const FAuthUserDTO&, User);
