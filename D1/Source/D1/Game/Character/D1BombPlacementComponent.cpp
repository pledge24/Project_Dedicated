// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/Character/D1BombPlacementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Core/D1LogChannels.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Game/D1Bomb.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/Character/D1BomberCharacter.h"

UD1BombPlacementComponent::UD1BombPlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Server RPC 라우팅용 — 액터 채널 위 복제 컴포넌트여야 클라 입력이 서버로 온다.
	SetIsReplicatedByDefault(true);
}

void UD1BombPlacementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 서버/클라 양쪽에서 실행해 양쪽 캡슐 스윕이 일치하도록.
	// (클라 이동 예측은 자체 MoveIgnoreActors 리스트를 따로 가짐.)
	UpdateIgnoredBombs();
}

void UD1BombPlacementComponent::ServerTryPlaceBomb_Implementation()
{
	// Server RPC + 서버 봇 직접 호출뿐이라 권위는 보장 — 남는 실패 원인은 오부착(비캐릭터 소유)뿐.
	AD1BomberCharacter* OwnerChar = GetBomberOwner();
	if (!ensureMsgf(OwnerChar, TEXT("BombPlacement: 소유자가 AD1BomberCharacter 아님")))
	{
		return;
	}

	AD1BomberPlayerState* PS = OwnerChar->GetPlayerState<AD1BomberPlayerState>();
	const FIntPoint Cell = UD1BomberGridLibrary::WorldToCell(OwnerChar->GetActorLocation());
	if (!CanPlaceBombAt(Cell, PS))
	{
		return;
	}

	const FVector SpawnLoc = UD1BomberGridLibrary::CellToWorldCenter(Cell, UD1BomberGridLibrary::CellHalf);
	FActorSpawnParameters Params;
	Params.Owner = OwnerChar;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AD1Bomb* Bomb = GetWorld()->SpawnActor<AD1Bomb>(BombClass, SpawnLoc, FRotator::ZeroRotator, Params);
	if (!Bomb)
	{
		return;
	}

	Bomb->SetRange(PS->GetFirePower()); // PS null은 CanPlaceBombAt이 이미 거부
	ActiveBombs.Add(Bomb);
	// 폭탄이 BeginPlay에서 겹친 캐릭터(소유자 포함)를 모두 IgnoredBombs에 등록함. 여기선 슬롯만 추적.
}

void UD1BombPlacementComponent::ServerPlaceBombForAI()
{
	// 봇 컨트롤러는 서버에만 존재 → RPC 왕복 없이 impl 직접 호출. 검증은 impl 내부가 담당.
	ServerTryPlaceBomb_Implementation();
}

void UD1BombPlacementComponent::NotifyBombDestroyed(AD1Bomb* Bomb)
{
	ActiveBombs.RemoveAll([Bomb](const TWeakObjectPtr<AD1Bomb>& W)
	{
		return !W.IsValid() || W.Get() == Bomb;
	});

	AD1BomberCharacter* OwnerChar = GetBomberOwner();
	if (Bomb && OwnerChar)
	{
		IgnoredBombs.Remove(Bomb);
		if (UCapsuleComponent* Cap = OwnerChar->GetCapsuleComponent())
		{
			Cap->IgnoreActorWhenMoving(Bomb, false);
		}
	}
}

void UD1BombPlacementComponent::AddIgnoredBomb(AD1Bomb* Bomb)
{
	AD1BomberCharacter* OwnerChar = GetBomberOwner();
	if (!Bomb || !OwnerChar)
	{
		return;
	}

	IgnoredBombs.Add(Bomb);
	if (UCapsuleComponent* Cap = OwnerChar->GetCapsuleComponent())
	{
		Cap->IgnoreActorWhenMoving(Bomb, true);
	}
}

bool UD1BombPlacementComponent::CanPlaceBombAt(const FIntPoint& Cell, AD1BomberPlayerState* PS)
{
	const AD1BomberCharacter* OwnerChar = GetBomberOwner();
	if (!OwnerChar || OwnerChar->IsStunned())
	{
		return false;
	}

	// 서버 검증부 — 판정 기준이 없으면 통과가 아니라 거부(fail-closed).
	// PS는 언포제스 직후 잔류 RPC로 잠시 없을 수 있어 조용히 거부, 서버에서 GS 부재는 불가능한 에러.
	if (!PS)
	{
		return false;
	}
	const AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (!ensureMsgf(GS, TEXT("BombPlacement: GameState 없음 — 설치 거부")))
	{
		return false;
	}

	if (GetActiveBombCount() >= PS->GetBombCapacity())
	{
		return false;
	}
	if (!BombClass)
	{
		UE_LOG(LogD1, Warning, TEXT("BombPlacement: BombClass not set"));
		return false;
	}

	if (GS->GetMatchPhase() != EBomberMatchPhase::Playing)
	{
		return false;
	}
	if (!PS->IsAlive())
	{
		return false;
	}
	if (!GS->IsInsideGrid(Cell))
	{
		return false;
	}
	if (GS->IsWallCell(Cell))
	{
		return false;
	}

	// 월드 폭탄 전역 검사해서 동일한 셀에 중복 설치 방지.
	bool bCellOccupied = false;
	UD1BomberGridLibrary::ForEachActorInCells<AD1Bomb>(GetWorld(), { Cell },
		[&bCellOccupied](AD1Bomb*)
		{
			bCellOccupied = true;
		});

	return !bCellOccupied;
}

int32 UD1BombPlacementComponent::GetActiveBombCount()
{
	for (int32 i = ActiveBombs.Num() - 1; i >= 0; --i)
	{
		if (!ActiveBombs[i].IsValid())
		{
			ActiveBombs.RemoveAtSwap(i);
		}
	}
	return ActiveBombs.Num();
}

void UD1BombPlacementComponent::UpdateIgnoredBombs()
{
	AD1BomberCharacter* OwnerChar = GetBomberOwner();
	if (!OwnerChar || IgnoredBombs.Num() == 0)
	{
		return;
	}

	UCapsuleComponent* Cap = OwnerChar->GetCapsuleComponent();
	if (!Cap)
	{
		return;
	}

	// "폭탄 영역 벗어남" 판정: 캡슐이 폭탄 박스 콜리전 밖으로 완전히 나간 뒤에야
	// 차단을 다시 켠다. 캡슐 반지름 + 박스 반폭 + 여유 거리로 계산해서,
	// IgnoreActorWhenMoving을 false로 되돌릴 때 캡슐이 박스에 끼어 튕겨나가는 거 방지.
	const float CapRadius = Cap->GetScaledCapsuleRadius();
	const float BombHalfExtent = UD1BomberGridLibrary::CellHalf; // 폭탄이 점유한 셀의 반폭
	const float ExitMargin = 5.f;
	const float ExitDistanceSquared = FMath::Square(CapRadius + BombHalfExtent + ExitMargin);

	const FVector MyLoc = OwnerChar->GetActorLocation();

	TArray<TWeakObjectPtr<AD1Bomb>> ToRemove;
	for (const TWeakObjectPtr<AD1Bomb>& WB : IgnoredBombs)
	{
		AD1Bomb* Bomb = WB.Get();
		if (!Bomb)
		{
			ToRemove.Add(WB);
			continue;
		}

		const FVector BombLoc = Bomb->GetActorLocation();
		const FVector Delta(MyLoc.X - BombLoc.X, MyLoc.Y - BombLoc.Y, 0.f);
		if (Delta.SizeSquared() > ExitDistanceSquared)
		{
			Cap->IgnoreActorWhenMoving(Bomb, false);
			ToRemove.Add(WB);
		}
	}

	for (const TWeakObjectPtr<AD1Bomb>& W : ToRemove)
	{
		IgnoredBombs.Remove(W);
	}
}

AD1BomberCharacter* UD1BombPlacementComponent::GetBomberOwner() const
{
	return Cast<AD1BomberCharacter>(GetOwner());
}
