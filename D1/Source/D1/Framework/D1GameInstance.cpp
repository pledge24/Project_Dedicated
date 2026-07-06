// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1GameInstance.h"

void UD1GameInstance::SetSession(const FString& InJwt, const FAuthUserDTO& InUser)
{
	CurrentJwt = InJwt;
	CurrentUser = InUser;
	bLoggedIn = true;
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
