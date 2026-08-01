// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Network/BackendTypes.h"

/** 서버가 message를 안 줬을 때 사용할 한글 fallback. */
class FBackendErrorMessages
{
public:
	static FString Lookup(EBackendErrorCode Code);

	/** 응답의 표시 문구 — 서버 메시지가 있으면 그대로, 없으면 코드 fallback. */
	static FString Resolve(const FBackendResponse& Response);
};
