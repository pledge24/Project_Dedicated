// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Online/BackendTypes.h"
#include "D1GameInstance.generated.h"

/**
 *  D1 GameInstance.
 *  맵 전환 사이에서 살아남는 세션 상태(JWT, 로그인 유저) 보관 전담.
 *  통신은 UBackendSubsystem이 담당, 여기는 데이터만.
 */
UCLASS()
class UD1GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	bool IsLoggedIn() const { return bLoggedIn; }

	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	const FAuthUserDTO& GetCurrentUser() const { return CurrentUser; }

	/** JWT 값 자체는 BP에 노출하지 않는다 (요청 헤더 주입은 Subsystem이 담당). */
	const FString& GetCurrentJwt() const { return CurrentJwt; }

	/** Subsystem이 인증 응답 받자마자 호출. */
	void SetSession(const FString& InJwt, const FAuthUserDTO& InUser);

	UFUNCTION(BlueprintCallable, Category = "D1|Session")
	void ClearSession();

private:
	UPROPERTY(Transient)
	FAuthUserDTO CurrentUser;

	UPROPERTY(Transient)
	bool bLoggedIn = false;

	// JWT는 UPROPERTY로 두지 않는다 (BP/리플렉션 노출 방지).
	FString CurrentJwt;
};
