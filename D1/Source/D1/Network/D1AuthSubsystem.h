// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1AuthSubsystem.generated.h"

class FJsonObject;

/**
 *  인증/프로필 전담 Subsystem (클라).
 *  회원가입/로그인(HTTP) + /api/auth/me 프로필 갱신. 응답을 받으면 GameInstance 세션을 갱신.
 */
UCLASS()
class UD1AuthSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ 외부 API — 인증 (BP에서 위젯이 호출)
	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted);

	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted);

	//~ 외부 API — 프로필
	/** GET /api/auth/me로 최신 프로필을 받아 GameInstance 캐시 갱신 후 OnProfileUpdated 방송. */
	UFUNCTION(BlueprintCallable, Category = "Backend|Profile")
	void RefreshMyProfile();

	//~ 외부 API — 세션 heartbeat
	/** GET /api/auth/heartbeat — 로비가 주기 호출. 401 SESSION_SUPERSEDED면 SessionSubsystem에 통지. */
	void SendHeartbeat();

	//~ 프로필 이벤트 (갱신 완료 구독용)
	UPROPERTY(BlueprintAssignable, Category = "Backend|Profile")
	FOnProfileUpdated OnProfileUpdated;

private:
	//~ 내부 헬퍼
	void SendAuthRequest(const FString& Path, const TSharedRef<FJsonObject>& Body, const FOnAuthCompleted& OnCompleted);
	void HandleAuthResponse(const FHttpResponsePtr& Res, bool bSucceeded, FOnAuthCompleted Forward);
	void HandleProfileResponse(const FHttpResponsePtr& Res, bool bSucceeded);
	void HandleHeartbeatResponse(const FHttpResponsePtr& Res, bool bSucceeded);
};
