// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "D1OnlineSettings.generated.h"

class UUserWidget;
class UWorld;

/**
 *  D1 온라인 설정.
 *  Project Settings > D1 > Online 에 노출. 값은 Config/DefaultGame.ini 에 영속화.
 *  런타임에서는 GetDefault<UD1OnlineSettings>()->BaseUrl 형태로 읽는다.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="D1 Online"))
class UD1OnlineSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override { return TEXT("D1"); }
	//~ End UDeveloperSettings Interface

	/** 백엔드 base URL. 실행 인자 `-BackendUrl=`이 있으면 그쪽이 이긴다(백엔드가 띄운 DS·PIE·QA). */
	UPROPERTY(Config, EditAnywhere, Category="Backend", meta=(DisplayName="Base URL"))
	FString BaseUrl = TEXT("http://127.0.0.1:3000");

	/** HTTP 요청 전체의 상한(초). 엔진 기본 총 타임아웃은 0(비활성)이라, 안 걸면 느리게 응답하는 서버에 UI가 무기한 묶인다. */
	UPROPERTY(Config, EditAnywhere, Category="Backend", meta=(DisplayName="Request Timeout (sec)", ClampMin="1"))
	float RequestTimeoutSec = 15.f;

	//~ 단일 세션(last-win) — 다른 기기 로그인 시 kick·복귀

	/** 로비 클라가 /api/auth/heartbeat를 호출하는 주기(초). 대체된 세션 감지 지연 상한. */
	UPROPERTY(Config, EditAnywhere, Category="Session", meta=(DisplayName="Heartbeat Interval (sec)", ClampMin="5"))
	float HeartbeatIntervalSec = 30.f;

	/** DS가 /api/match/:id/kicks를 폴링하는 주기(초). 게임중 강제 회수 지연 상한. */
	UPROPERTY(Config, EditAnywhere, Category="Session", meta=(DisplayName="Kick Poll Interval (sec)", ClampMin="1"))
	float KickPollIntervalSec = 5.f;

	/** 세션 무효화 시 돌아갈 로그인(프론트엔드) 맵. */
	UPROPERTY(Config, EditAnywhere, Category="Session", meta=(DisplayName="Frontend Map"))
	TSoftObjectPtr<UWorld> FrontendMap;

	/** DS 접속이 끊겼을 때 돌아갈 로비 맵. 세션은 유지되므로 로그인 화면이 아니라 여기로 보낸다. 미설정 시 FrontendMap으로 대체. */
	UPROPERTY(Config, EditAnywhere, Category="Session", meta=(DisplayName="Lobby Map"))
	TSoftObjectPtr<UWorld> LobbyMap;

	/** 세션 무효화 알림 모달 위젯(WBP_SystemNotice). */
	UPROPERTY(Config, EditAnywhere, Category="Session", meta=(DisplayName="System Notice Widget"))
	TSoftClassPtr<UUserWidget> SystemNoticeWidgetClass;
};
