// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 MaxFirePower = 10;
	constexpr int32 MaxBombCapacity = 10;
	constexpr int32 MaxSpeedLevel = 5;
}

void AD1BomberPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AD1BomberPlayerState, Lives);
	DOREPLIFETIME(AD1BomberPlayerState, bIsAlive);
	DOREPLIFETIME(AD1BomberPlayerState, Placement);
	DOREPLIFETIME(AD1BomberPlayerState, PlayerSlotIndex);
	// 화력·폭탄수는 소유자 HUD 표시용 → 소유 클라에만 복제(대역폭↓). 서버 권위 값은 그대로.
	DOREPLIFETIME_CONDITION(AD1BomberPlayerState, FirePower, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AD1BomberPlayerState, BombCapacity, COND_OwnerOnly);
	DOREPLIFETIME(AD1BomberPlayerState, SpeedLevel);
}

bool AD1BomberPlayerState::ApplyHit()
{
	if (!HasAuthority() || !bIsAlive)
	{
		return false;
	}

	Lives = FMath::Max(0, Lives - 1);
	OnRep_Lives(); // Listen Server 대응

	if (Lives <= 0)
	{
		bIsAlive = false;
		OnRep_bIsAlive(); // Listen Server 대응

		// 사망 전환의 매치 처리(등수·승패)는 상태 주인인 PS가 직접 GM에 보고.
		if (AD1BomberGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AD1BomberGameMode>() : nullptr)
		{
			GM->NotifyPlayerDied(this);
		}
		return true;
	}
	return false;
}

void AD1BomberPlayerState::SetPlayerSlotIndex(int32 NewIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	// -1(미배정 리셋) 또는 0~3만 허용. 범위 밖은 거부 — clamp하면 두 명이 같은 슬롯으로 몰림.
	if (NewIndex < -1 || NewIndex > 3)
	{
		return;
	}
	PlayerSlotIndex = NewIndex;
	OnRep_PlayerSlotIndex(); // Listen Server 대응
}

void AD1BomberPlayerState::SetPlacement(int32 NewPlacement)
{
	if (!HasAuthority())
	{
		return;
	}
	Placement = NewPlacement;
}

void AD1BomberPlayerState::SetBackendUserId(int64 NewUserId)
{
	if (!HasAuthority())
	{
		return;
	}
	BackendUserId = NewUserId;
}

void AD1BomberPlayerState::AddFirePower(int32 Delta)
{
	if (!HasAuthority())
	{
		return;
	}
	FirePower = FMath::Clamp(FirePower + Delta, 0, MaxFirePower);
}

void AD1BomberPlayerState::AddBombCapacity(int32 Delta)
{
	if (!HasAuthority())
	{
		return;
	}
	BombCapacity = FMath::Clamp(BombCapacity + Delta, 1, MaxBombCapacity);
}

void AD1BomberPlayerState::AddSpeedLevel(int32 Delta)
{
	if (!HasAuthority())
	{
		return;
	}
	SpeedLevel = FMath::Clamp(SpeedLevel + Delta, 0, MaxSpeedLevel);
	OnRep_SpeedLevel(); // Listen Server 대응 — 서버 캐릭터도 속도 반영
}

void AD1BomberPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();
	OnPlayerNameChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_Lives()
{
	OnLivesChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_bIsAlive()
{
	OnAliveStateChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_SpeedLevel()
{
	OnSpeedLevelChanged.Broadcast();
}

void AD1BomberPlayerState::OnRep_PlayerSlotIndex()
{
	OnSlotIndexChanged.Broadcast();

	// 컨테이너 위젯이 한 곳에서 카드 전체를 다시 그릴 수 있게 GameState 디스패처도 트리거.
	if (UWorld* World = GetWorld())
	{
		if (AD1BomberGameState* GS = World->GetGameState<AD1BomberGameState>())
		{
			GS->MarkPlayerCardsDirty();
		}
	}
}
