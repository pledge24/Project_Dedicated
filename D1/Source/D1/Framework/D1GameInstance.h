// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Network/BackendTypes.h"
#include "D1GameInstance.generated.h"

class UNetDriver;
class UWorld;

/**
 *  D1 GameInstance.
 *  맵 전환 사이에서 살아남는 세션 상태(JWT, 로그인 유저) 보관 전담(100% 유저용).
 *  통신은 백엔드 Subsystem(Auth/Matchmaking/Result)이 담당, 여기는 데이터만.
 *  예외로 엔진 전역 네트워크 실패 이벤트는 여기서만 받을 수 있어 구독하고 Session Subsystem에 넘긴다.
 */
UCLASS()
class UD1GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	//~ Begin UGameInstance Interface
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual void LoadComplete(const float LoadTime, const FString& MapName) override;
	//~ End UGameInstance Interface

	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	bool IsLoggedIn() const { return bLoggedIn; }

	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	const FAuthUserDTO& GetCurrentUser() const { return CurrentUser; }

	/** JWT 값 자체는 BP에 노출하지 않는다 (요청 헤더 주입은 Subsystem이 담당). */
	const FString& GetCurrentJwt() const { return CurrentJwt; }

	/** Subsystem이 인증 응답 받자마자 호출. */
	void SetSession(const FString& InJwt, const FAuthUserDTO& InUser);

	/** 세션 유지한 채 유저 정보만 최신화(/api/auth/me 갱신용). JWT·로그인 상태 보존. */
	void UpdateUserProfile(const FAuthUserDTO& InUser);

	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	void ClearSession();

private:
	/** DS 접속이 끊겼을 때(크래시 등). 엔진이 넷드라이버 실패를 알리는 유일한 경로. */
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	/** DS로 travel 자체가 실패했을 때(주소 불가·거절). */
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;

	UPROPERTY(Transient)
	FAuthUserDTO CurrentUser;

	UPROPERTY(Transient)
	bool bLoggedIn = false;

	/** UPROPERTY로 두지 않음 — BP/리플렉션 노출 방지. */
	FString CurrentJwt;
};
