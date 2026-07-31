// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 뷰포트 ZOrder 계층 — AddToViewport에 리터럴 대신 이 상수를 쓴다(파일 간 주석 동기화 제거). */
namespace D1UILayer
{
	/** 메뉴 배경 — 어떤 콘텐츠보다 뒤. */
	inline constexpr int32 Background = -1;
	/** 화면 콘텐츠(로그인/로비 등) 기본층. */
	inline constexpr int32 Content = 0;
	/** 상시 종료 버튼 — 콘텐츠 위, 팝업 아래. */
	inline constexpr int32 QuitButton = 5;
	/** 랭킹 등 일반 팝업. */
	inline constexpr int32 Popup = 10;
	/** 시스템 모달(세션 알림) — 전부 위. */
	inline constexpr int32 Modal = 100;
}
