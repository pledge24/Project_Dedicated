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
	//~ Begin USubsystem Interface
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	//~ 외부 API — 매칭 (BP에서 위젯이 호출)
	/**
	 *  진행 중이던 매치가 있으면 그 DS로 되돌아간다(GET /api/match/current).
	 *  match:found 푸시는 1회성이라 그 순간 끊기면 복구 수단이 없다 — 로비 진입 시 1회 확인이 그 창을 메운다.
	 *  백엔드가 "결과 미저장 + 탈주 미정산"인 매치만 알려주므로 끝난 경기로 되돌아가지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Backend|Match")
	void CheckRejoinableMatch();

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

	//~ 내부 헬퍼 — DS 입장 (신규 성사·재입장 공용)
	void HandleRejoinResponse(const FString& Body);
	void TravelToMatch(const FMatchFoundDTO& Match);

	//~ 내부 상태 (비-UPROPERTY)
	TSharedPtr<IWebSocket> MatchSocket;
	EMatchmakingState MatchmakingState = EMatchmakingState::Idle;
};
