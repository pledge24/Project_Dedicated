// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/D1SessionSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Core/D1LogChannels.h"
#include "Engine/GameInstance.h"
#include "Framework/D1GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Network/D1MatchmakingSubsystem.h"
#include "Network/D1OnlineSettings.h"
#include "UI/D1UILayers.h"
#include "UI/D1UWSystemNotice.h"

void UD1SessionSubsystem::NotifySessionSuperseded()
{
	// 멱등 — 여러 감지 경로(heartbeat·WS·게임중 kick)가 동시에 도달해도 1회만 처리.
	if (bHandled)
	{
		return;
	}
	bHandled = true;
	bReturnToLogin = true;

	UE_LOG(LogD1, Warning, TEXT("[Session] 다른 기기 로그인으로 세션 대체 — 로그인 화면 복귀"));

	// 큐 대기 중이면 매칭 WS 정리(로비 heartbeat가 감지한 경우 등). 이미 닫혔으면 no-op.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UD1MatchmakingSubsystem* Matchmaking = GI->GetSubsystem<UD1MatchmakingSubsystem>())
		{
			Matchmaking->CancelMatchmaking();
		}
	}

	if (!ShowNotice(
		NSLOCTEXT("Session", "SupersededTitle", "시스템 알림"),
		NSLOCTEXT("Session", "SupersededMsg", "다른 기기에서 로그인하여\n접속이 종료되었습니다.")))
	{
		// 팝업을 못 띄웠으면(설정 누락/로컬 PC 없음) 즉시 복귀 — 갇힘 방지.
		TravelToDestination();
	}
}

void UD1SessionSubsystem::NotifyMatchDisconnected()
{
	// 결과 화면에서 로비로 나가는 정상 경로에서도 넷드라이버가 내려가며 실패 이벤트가 뜰 수 있다.
	if (bIntentionalTravel)
	{
		UE_LOG(LogD1, Verbose, TEXT("[Session] 의도한 이탈 중 접속 종료 — 무시"));

		return;
	}

	if (bHandled)
	{
		return;
	}
	bHandled = true;
	bReturnToLogin = false;

	UE_LOG(LogD1, Warning, TEXT("[Session] 게임 서버 접속 끊김 — 로비 복귀"));

	if (!ShowNotice(
		NSLOCTEXT("Session", "DisconnectedTitle", "연결 종료"),
		NSLOCTEXT("Session", "DisconnectedMsg", "게임 서버와의 연결이 끊어졌습니다.\n로비로 돌아갑니다.")))
	{
		TravelToDestination();
	}
}

void UD1SessionSubsystem::NotifyMapLoaded()
{
	// 이탈이 끝나 새 맵에 도착했다 — 다음 사이클을 정상 감지하려면 두 가드 모두 풀어야 한다.
	bIntentionalTravel = false;
	bHandled = false;
}

void UD1SessionSubsystem::TravelToLobby()
{
	// 이동이 일으키는 넷드라이버 종료(DS에서 복귀 등)가 장애로 잡히지 않게 먼저 표시.
	BeginIntentionalTravel();
	OpenLobbyMap();
}

void UD1SessionSubsystem::TravelToFrontend()
{
	BeginIntentionalTravel();
	OpenFrontendMap();
}

void UD1SessionSubsystem::HandleNoticeConfirmed()
{
	if (NoticeWidget)
	{
		NoticeWidget->RemoveFromParent();
		NoticeWidget = nullptr;
	}

	TravelToDestination();
}

bool UD1SessionSubsystem::ShowNotice(const FText& Title, const FText& Message)
{
	// 위젯 클래스는 온라인 설정에서 로드(C++ 하드코딩 경로 금지).
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	const UD1OnlineSettings* Settings = GetDefault<UD1OnlineSettings>();
	if (!PC || !Settings || Settings->SystemNoticeWidgetClass.IsNull())
	{
		return false;
	}

	UClass* NoticeClass = Settings->SystemNoticeWidgetClass.LoadSynchronous();
	if (!NoticeClass)
	{
		return false;
	}

	// 확인 델리게이트 없는 모달은 FInputModeUIOnly와 함께 영구 소프트락 —
	// 파생 확인 전엔 아무것도 띄우지 않고 false 반환(호출자의 즉시 복귀 폴백 발동).
	UD1UWSystemNotice* Notice = Cast<UD1UWSystemNotice>(CreateWidget<UUserWidget>(PC, NoticeClass));
	if (!ensureMsgf(Notice, TEXT("[Session] SystemNoticeWidgetClass가 UD1UWSystemNotice 파생이 아님")))
	{
		return false;
	}

	Notice->SetNotice(Title, Message);
	Notice->OnSystemNoticeConfirmed.AddDynamic(this, &UD1SessionSubsystem::HandleNoticeConfirmed);

	NoticeWidget = Notice;
	NoticeWidget->AddToViewport(D1UILayer::Modal);

	// 모달 조작을 위해 UI 입력 + 커서 (인게임 GameOnly 상태에서도 확인 클릭 가능).
	PC->SetShowMouseCursor(true);
	PC->SetInputMode(FInputModeUIOnly());

	return true;
}

void UD1SessionSubsystem::TravelToDestination()
{
	// 이 시점부터의 넷드라이버 종료는 우리가 일으킨 것 — 다시 장애로 잡히면 팝업이 겹친다.
	BeginIntentionalTravel();

	if (bReturnToLogin)
	{
		ReturnToLogin();

		return;
	}

	OpenLobbyMap();
}

void UD1SessionSubsystem::ReturnToLogin()
{
	if (UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
	{
		GI->ClearSession();
	}

	OpenFrontendMap();
}

void UD1SessionSubsystem::OpenLobbyMap()
{
	const UD1OnlineSettings* Settings = GetDefault<UD1OnlineSettings>();
	if (Settings && !Settings->LobbyMap.IsNull())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, Settings->LobbyMap);

		return;
	}

	// 로비 맵이 없으면 세션이 살아있어도 갈 곳이 없다 — 로그인 화면으로라도 내보낸다(갇힘 방지).
	UE_LOG(LogD1, Error, TEXT("[Session] LobbyMap 미설정 — 로그인 화면으로 폴백 (Project Settings > D1 > Session)"));
	ReturnToLogin();
}

void UD1SessionSubsystem::OpenFrontendMap()
{
	const UD1OnlineSettings* Settings = GetDefault<UD1OnlineSettings>();
	if (Settings && !Settings->FrontendMap.IsNull())
	{
		// TRAVEL_Absolute — DS 접속(게임중 kick)이나 로비 어디서든 프론트엔드 맵을 새로 연다.
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, Settings->FrontendMap);

		return;
	}

	UE_LOG(LogD1, Error, TEXT("[Session] FrontendMap 미설정 — 로그인 화면 복귀 불가 (Project Settings > D1 > Session)"));
}
