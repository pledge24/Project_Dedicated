// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1GameInstance.h"

#include "Core/D1LogChannels.h"
#include "Engine/Engine.h"
#include "Network/D1SessionSubsystem.h"

void UD1GameInstance::Init()
{
	Super::Init();

	// 넷드라이버 실패는 엔진 전역 이벤트로만 통보된다 — DS가 게임 중 죽어도 클라가 알 길이 이것뿐이다.
	// (백엔드 매칭 WS는 match:found 직후 닫혀 있어 푸시를 받을 채널이 없다.)
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UD1GameInstance::HandleNetworkFailure);
		TravelFailureHandle  = GEngine->OnTravelFailure().AddUObject(this, &UD1GameInstance::HandleTravelFailure);
	}
}

void UD1GameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	Super::Shutdown();
}

void UD1GameInstance::LoadComplete(const float LoadTime, const FString& MapName)
{
	Super::LoadComplete(LoadTime, MapName);

	// 새 맵 도착 = 한 이탈 사이클 종료. 다음 끊김을 정상 감지하려면 세션 가드를 풀어야 한다.
	if (UD1SessionSubsystem* Session = GetSubsystem<UD1SessionSubsystem>())
	{
		Session->NotifyMapLoaded();
	}
}

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

void UD1GameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// DS도 클라 타임아웃 시 이 전역 이벤트를 받는다 — 아래 복구(로비/로그인 travel)는 클라 전용이라
	// 서버에서 실행되면 진행 중인 매치 맵을 통째로 버린다. 서버의 클라 이탈은 GameMode::Logout 소관.
	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogD1, Warning, TEXT("[Net] DS 네트워크 실패 무시 type=%s msg=%s"),
			ENetworkFailure::ToString(FailureType), *ErrorString);

		return;
	}

	UE_LOG(LogD1, Warning, TEXT("[Net] 네트워크 실패 type=%s msg=%s"),
		ENetworkFailure::ToString(FailureType), *ErrorString);

	// 의도한 이탈인지·중복인지 판단은 Session Subsystem이 한다(세션 대체 경로와 가드를 공유).
	if (UD1SessionSubsystem* Session = GetSubsystem<UD1SessionSubsystem>())
	{
		Session->NotifyMatchDisconnected();
	}
}

void UD1GameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	// HandleNetworkFailure와 동일 — DS에서 클라 전용 복구 travel 금지.
	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogD1, Warning, TEXT("[Net] DS Travel 실패 무시 type=%s msg=%s"),
			ETravelFailure::ToString(FailureType), *ErrorString);

		return;
	}

	UE_LOG(LogD1, Warning, TEXT("[Net] Travel 실패 type=%s msg=%s"),
		ETravelFailure::ToString(FailureType), *ErrorString);

	if (UD1SessionSubsystem* Session = GetSubsystem<UD1SessionSubsystem>())
	{
		Session->NotifyMatchDisconnected();
	}
}
