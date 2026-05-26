// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/BackendErrorMessages.h"

FString FBackendErrorMessages::Lookup(EBackendErrorCode Code)
{
	switch (Code)
	{
	case EBackendErrorCode::ValidationFailed:
		return TEXT("입력 형식이 올바르지 않습니다.");
	case EBackendErrorCode::InvalidCredentials:
		return TEXT("ID 또는 비밀번호가 일치하지 않습니다.");
	case EBackendErrorCode::DuplicateLoginId:
		return TEXT("이미 사용 중인 ID입니다.");
	case EBackendErrorCode::DuplicateNickname:
		return TEXT("이미 사용 중인 닉네임입니다.");
	case EBackendErrorCode::RateLimited:
		return TEXT("요청이 너무 잦습니다. 잠시 후 다시 시도해주세요.");
	case EBackendErrorCode::NetworkError:
		return TEXT("서버에 연결할 수 없습니다.");
	case EBackendErrorCode::InternalError:
		return TEXT("서버 오류가 발생했습니다.");
	case EBackendErrorCode::None:
	case EBackendErrorCode::Unknown:
	default:
		return TEXT("알 수 없는 오류가 발생했습니다.");
	}
}
