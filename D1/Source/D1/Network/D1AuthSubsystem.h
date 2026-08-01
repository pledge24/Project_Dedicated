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

//~ 인증
public:
	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted);

	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted);

private:
	void SendAuthRequest(const FString& Path, const TSharedRef<FJsonObject>& Body, const FOnAuthCompleted& OnCompleted);
	void HandleAuthResponse(const FHttpResponsePtr& Res, bool bSucceeded, FOnAuthCompleted Forward);

//~ 프로필
public:
	/** GET /api/auth/me로 최신 프로필을 받아 GameInstance 캐시 갱신 후 OnProfileUpdated 방송. */
	UFUNCTION(BlueprintCallable, Category = "Backend|Profile")
	void RefreshMyProfile();

	UPROPERTY(BlueprintAssignable, Category = "Backend|Profile")
	FOnProfileUpdated OnProfileUpdated;

private:
	void HandleProfileResponse(const FHttpResponsePtr& Res, bool bSucceeded);

//~ 세션 heartbeat
public:
	/** GET /api/auth/heartbeat — 로비가 주기 호출. 401 SESSION_SUPERSEDED면 SessionSubsystem에 통지. */
	void SendHeartbeat();

private:
	void HandleHeartbeatResponse(const FHttpResponsePtr& Res, bool bSucceeded);
};
