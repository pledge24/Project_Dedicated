// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1MatchmakingSubsystem.generated.h"

class IWebSocket;

/**
 *  매칭 전담 Subsystem (클라). /ws/match WebSocket로 큐 입장·매칭 성사 푸시를 처리.
 *  match:found 시 할당된 DS로 ClientTravel. Subsystem은 travel을 가로질러 살아남는다.
 */
UCLASS()
class UD1MatchmakingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Deinitialize() override;

	//~ 외부 API — 매칭 (BP에서 위젯이 호출)
	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	void StartMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	void CancelMatchmaking();

	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	EMatchmakingState GetMatchmakingState() const { return MatchmakingState; }

	//~ 매칭 이벤트 (서버 푸시 구독용)
	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnMatchFound OnMatchFound;

	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnQueueJoined OnQueueJoined;

	UPROPERTY(BlueprintAssignable, Category = "Backend|Match")
	FOnMatchmakingError OnMatchmakingError;

private:
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
