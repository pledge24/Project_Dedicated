// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/BackendTypes.h"

/** 서버가 message를 안 줬을 때 사용할 한글 fallback. */
class FBackendErrorMessages
{
public:
	static FString Lookup(EBackendErrorCode Code);
};
