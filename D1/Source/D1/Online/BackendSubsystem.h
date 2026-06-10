// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/BackendTypes.h"
#include "BackendSubsystem.generated.h"

class FJsonObject;
class IWebSocket;

/**
 *  백엔드 통신 전담 Subsystem.
 *  HTTP 요청(회원가입/로그인) + 매칭 WebSocket(/ws/match)을 처리.
 *  응답을 받자마자 GameInstance->SetSession, 매칭 푸시는 멀티캐스트 델리게이트로 방송.
 */
UCLASS()
class UBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ 외부 API — 인증 (BP에서 위젯이 호출)
	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted);

	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted);

	//~ 외부 API — 매칭
	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	void StartMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	void CancelMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	EMatchmakingState GetMatchmakingState() const { return MatchmakingState; }

	//~ 외부 API — 프로필
	/** GET /api/auth/me로 최신 프로필을 받아 GameInstance 캐시 갱신 후 OnProfileUpdated 방송. */
	UFUNCTION(BlueprintCallable, Category = "Backend|Profile")
	void RefreshMyProfile();

	UFUNCTION(BlueprintCallable, Category = "Backend")
	const FString& GetBaseUrl() const;

	//~ 외부 API — 매치 결과 보고 (DS 전용, C++ 호출). 매치별 서버 토큰을 Bearer로 첨부.
	void ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
		int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players);

	//~ 매칭 이벤트 (서버 푸시 구독용)
	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnMatchFound OnMatchFound;

	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnQueueJoined OnQueueJoined;

	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnMatchmakingError OnMatchmakingError;

	//~ 프로필 이벤트 (갱신 완료 구독용)
	UPROPERTY(BlueprintAssignable, Category = "Backend|Profile")
	FOnProfileUpdated OnProfileUpdated;

private:
	//~ 내부 헬퍼 — HTTP 인증
	TSharedRef<IHttpRequest> BuildPostJson(const FString& Path, const TSharedRef<FJsonObject>& Body, bool bAttachAuth) const;
	void HandleAuthResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded, FOnAuthCompleted Forward);
	void HandleProfileResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded);
	static EBackendErrorCode ParseErrorCode(const FString& CodeStr);

	//~ 내부 헬퍼 — 매칭 WebSocket
	FString BuildMatchWsUrl() const;
	void SendType(const FString& Type);
	void CloseMatchSocket();
	void HandleSocketConnected();
	void HandleSocketMessage(const FString& Message);
	void HandleSocketConnectionError(const FString& Error);
	void HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

	//~ 내부 상태 (비-UPROPERTY)
	TSharedPtr<IWebSocket> MatchSocket;
	EMatchmakingState MatchmakingState = EMatchmakingState::Idle;
};
