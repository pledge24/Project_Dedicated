// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1GameInstance.h"

#include "Network/D1SessionSubsystem.h"

void UD1GameInstance::SetSession(const FString& InJwt, const FAuthUserDTO& InUser)
{
	CurrentJwt = InJwt;
	CurrentUser = InUser;
	bLoggedIn = true;

	// 새 로그인 = 새 세션 → 단일 세션 대체 가드 리셋(이전 kick 후 재로그인 시 다음 감지 정상화).
	if (UD1SessionSubsystem* Session = GetSubsystem<UD1SessionSubsystem>())
	{
		Session->ResetSupersededGuard();
	}
}

void UD1GameInstance::UpdateUserProfile(const FAuthUserDTO& InUser)
{
	CurrentUser = InUser;
}

void UD1GameInstance::ClearSession()
{
	CurrentJwt.Empty();
	CurrentUser = FAuthUserDTO();
	bLoggedIn = false;
}
