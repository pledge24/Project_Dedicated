// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Online/BackendTypes.h"
#include "BackendSubsystem.generated.h"

class FJsonObject;

/**
 *  백엔드 통신 전담 Subsystem.
 *  HTTP 요청(회원가입/로그인)을 처리하고, 응답을 받자마자 GameInstance->SetSession 호출.
 *  WebSocket은 다음 슬라이스에서 추가 예정.
 */
UCLASS()
class UBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ 외부 API (BP에서 위젯이 호출)
	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Register(const FString& LoginId, const FString& Password, const FString& Nickname, const FOnAuthCompleted& OnCompleted);

	UFUNCTION(BlueprintCallable, Category = "Backend|Auth")
	void Login(const FString& LoginId, const FString& Password, const FOnAuthCompleted& OnCompleted);

	UFUNCTION(BlueprintCallable, Category = "Backend")
	const FString& GetBaseUrl() const { return BaseUrl; }

private:
	//~ 내부 헬퍼
	TSharedRef<IHttpRequest> BuildPostJson(const FString& Path, const TSharedRef<FJsonObject>& Body, bool bAttachAuth) const;
	void HandleAuthResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSucceeded, FOnAuthCompleted Forward);
	static EBackendErrorCode ParseErrorCode(const FString& CodeStr);

	//~ 설정 데이터 (디테일 패널에서 dev/prod 전환)
	UPROPERTY(EditDefaultsOnly, Category = "Backend", meta = (AllowPrivateAccess = "true"))
	FString BaseUrl = TEXT("http://127.0.0.1:3000");
};
