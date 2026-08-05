// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "D1SessionSubsystem.generated.h"

class UUserWidget;

/**
 *  세션 이탈 공용 처리 (클라).
 *  단일 세션 대체(heartbeat/매칭 WS/게임중 kick)와 DS 접속 끊김(크래시·travel 실패)이 모두 여기로 수렴 —
 *  알림 모달 표시 후 [확인] 시 목적지로 복귀. 감지 경로가 여럿이라 멱등(중복 트리거 무시).
 */
UCLASS()
class UD1SessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 세션이 다른 기기 로그인으로 대체됨 — 알림 후 [확인] 시 세션 정리 + 로그인 화면 복귀. */
	void NotifySessionSuperseded();

	/** DS 접속이 끊김(크래시·travel 실패) — 알림 후 [확인] 시 로비 복귀. 세션은 유지한다. */
	void NotifyMatchDisconnected();

	/**
	 * 의도한 이탈(결과 화면 → 로비, 로그인 복귀) 직전에 호출.
	 * 정상 복귀에서도 넷드라이버가 내려가며 실패 이벤트가 뜰 수 있어, 그걸 장애로 오인하지 않게 한다.
	 */
	void BeginIntentionalTravel() { bIntentionalTravel = true; }

	/** 맵 로드 완료 — 한 이탈 사이클이 끝났으므로 다음 감지를 위해 가드를 푼다. GameInstance가 호출. */
	void NotifyMapLoaded();

	/** 새 로그인 성립 시 GameInstance가 호출 — 다음 대체 감지를 위해 가드 리셋. */
	void ResetSupersededGuard() { bHandled = false; }

	/** 의도한 이동으로 로비 맵을 연다(로그인 성공·결과 화면 복귀 공용, 맵은 온라인 설정 단일 출처). */
	void TravelToLobby();

	/** 의도한 이동으로 프론트엔드(로그인) 맵을 연다(비로그인 방어 등). 세션은 건드리지 않는다. */
	void TravelToFrontend();

protected:
	UFUNCTION()
	void HandleNoticeConfirmed();

private:
	/** 알림 모달 표시. 못 띄웠으면 false — 호출측이 즉시 복귀시켜 갇힘을 막는다. */
	bool ShowNotice(const FText& Title, const FText& Message);
	/** [확인] 또는 모달 실패 시의 목적지 분기. */
	void TravelToDestination();
	void ReturnToLogin();
	void OpenLobbyMap();
	void OpenFrontendMap();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> NoticeWidget;

	/** true면 로그인 화면(세션 대체), false면 로비(접속 끊김). */
	bool bReturnToLogin = true;

	bool bHandled = false;
	bool bIntentionalTravel = false;
};
