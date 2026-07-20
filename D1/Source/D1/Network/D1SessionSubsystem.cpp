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
#include "UI/D1UWSystemNotice.h"

void UD1SessionSubsystem::NotifySessionSuperseded()
{
	// 멱등 — 여러 감지 경로(heartbeat·WS·게임중 kick)가 동시에 도달해도 1회만 처리.
	if (bHandled)
	{
		return;
	}
	bHandled = true;

	UE_LOG(LogD1, Warning, TEXT("[Session] 다른 기기 로그인으로 세션 대체 — 로그인 화면 복귀"));

	UGameInstance* GI = GetGameInstance();

	// 큐 대기 중이면 매칭 WS 정리(로비 heartbeat가 감지한 경우 등). 이미 닫혔으면 no-op.
	if (GI)
	{
		if (UD1MatchmakingSubsystem* Matchmaking = GI->GetSubsystem<UD1MatchmakingSubsystem>())
		{
			Matchmaking->CancelMatchmaking();
		}
	}

	// 알림 모달 표시. 위젯 클래스는 온라인 설정에서 로드(C++ 하드코딩 경로 금지).
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	const UD1OnlineSettings* Settings = GetDefault<UD1OnlineSettings>();
	if (PC && Settings && !Settings->SystemNoticeWidgetClass.IsNull())
	{
		if (UClass* NoticeClass = Settings->SystemNoticeWidgetClass.LoadSynchronous())
		{
			NoticeWidget = CreateWidget<UUserWidget>(PC, NoticeClass);
			if (NoticeWidget)
			{
				// ZOrder 100 → 랭킹 팝업(10)·종료 버튼(5)보다 위.
				NoticeWidget->AddToViewport(100);

				if (UD1UWSystemNotice* Notice = Cast<UD1UWSystemNotice>(NoticeWidget))
				{
					Notice->SetNotice(
						NSLOCTEXT("Session", "SupersededTitle", "시스템 알림"),
						NSLOCTEXT("Session", "SupersededMsg", "다른 기기에서 로그인하여\n접속이 종료되었습니다."));
					Notice->OnConfirmed.AddDynamic(this, &UD1SessionSubsystem::OnConfirmReturnToLogin);
				}

				// 모달 조작을 위해 UI 입력 + 커서 (인게임 GameOnly 상태에서도 확인 클릭 가능).
				PC->SetShowMouseCursor(true);
				PC->SetInputMode(FInputModeUIOnly());
			}
		}
	}

	// 팝업을 못 띄웠으면(설정 누락/로컬 PC 없음) 즉시 복귀 — 갇힘 방지.
	if (!NoticeWidget)
	{
		ReturnToLogin();
	}
}

void UD1SessionSubsystem::OnConfirmReturnToLogin()
{
	if (NoticeWidget)
	{
		NoticeWidget->RemoveFromParent();
		NoticeWidget = nullptr;
	}

	ReturnToLogin();
}

void UD1SessionSubsystem::ReturnToLogin()
{
	if (UD1GameInstance* GI = Cast<UD1GameInstance>(GetGameInstance()))
	{
		GI->ClearSession();
	}

	const UD1OnlineSettings* Settings = GetDefault<UD1OnlineSettings>();
	if (Settings && !Settings->FrontendMap.IsNull())
	{
		// TRAVEL_Absolute — DS 접속(게임중 kick)이나 로비 어디서든 프론트엔드 맵을 새로 연다.
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, Settings->FrontendMap);
	}
	else
	{
		UE_LOG(LogD1, Error, TEXT("[Session] FrontendMap 미설정 — 로그인 화면 복귀 불가 (Project Settings > D1 > Session)"));
	}
}
