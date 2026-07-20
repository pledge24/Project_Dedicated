// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "D1SessionSubsystem.generated.h"

class UUserWidget;

/**
 *  단일 세션(last-win) 공용 처리 (클라).
 *  로비 heartbeat / 매칭 WS / 게임중 kick 어느 경로에서든 세션 대체가 감지되면 여기로 수렴 —
 *  알림 모달 표시 후 [확인] 시 세션 정리 + 로그인 화면 복귀. 멱등(중복 트리거 무시).
 */
UCLASS()
class UD1SessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 세션이 다른 기기 로그인으로 대체됨 — 알림 후 [확인] 시 로그인 화면 복귀. 어느 경로에서든 호출, 멱등. */
	void NotifySessionSuperseded();

	/** 새 로그인 성립 시 GameInstance가 호출 — 다음 대체 감지를 위해 가드 리셋. */
	void ResetSupersededGuard() { bHandled = false; }

protected:
	UFUNCTION()
	void OnConfirmReturnToLogin();

private:
	void ReturnToLogin();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> NoticeWidget;

	bool bHandled = false;
};
