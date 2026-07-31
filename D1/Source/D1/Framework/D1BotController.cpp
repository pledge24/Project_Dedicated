// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BotController.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Core/D1LogChannels.h"
#include "Game/Character/D1BomberCharacter.h"
#include "Game/Character/D1BombPlacementComponent.h"
#include "Game/D1Bomb.h"
#include "Game/D1ExplosionHazard.h"
#include "Game/D1BomberGridLibrary.h"
#include "Game/D1PowerupPickup.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"

AD1BotController::AD1BotController()
{
	// AIController 기본값 false — 켜야 PlayerState가 생성돼 PlayerArray에 잡힌다(Lyra 봇 생성 패턴).
	bWantsPlayerState = true;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AD1BotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 봇 사고는 서버 권위 전용(AIController는 서버에만 존재하지만 방어적으로 명시).
	if (!HasAuthority())
	{
		return;
	}

	AD1BomberCharacter* Bot = GetPawn<AD1BomberCharacter>();
	if (!Bot)
	{
		return;
	}

	const AD1BomberGameState* GS = GetWorld() ? GetWorld()->GetGameState<AD1BomberGameState>() : nullptr;
	if (!GS || GS->MatchPhase != EBomberMatchPhase::Playing)
	{
		return;
	}

	const AD1BomberPlayerState* PS = GetPlayerState<AD1BomberPlayerState>();
	if (PS && !PS->IsAlive())
	{
		return;
	}

	// 사고는 저빈도(누산기)로 throttle, 조향(AddMovementInput 소비)은 매 프레임.
	ThinkAccumulatorSec += DeltaSeconds;
	if (ThinkAccumulatorSec >= ThinkIntervalSec)
	{
		ThinkAccumulatorSec = 0.f;
		Think(Bot, GS, PS);
	}
	SteerAlongPath(Bot);
}

void AD1BotController::Think(AD1BomberCharacter* Bot, const AD1BomberGameState* GS, const AD1BomberPlayerState* PS)
{
	EnsureSeeded(PS);
	BuildDangerMap(Bot, GS);

	const FIntPoint Cur = UD1BomberGridLibrary::WorldToCell(Bot->GetActorLocation());
	// 도주는 아직 안 터진 폭탄 십자만 달려 통과 가능, 이미 터진 hazard는 통과 금지. SEEK/HUNT는 모든 위험 회피.
	auto Passable = [this, GS](FIntPoint C) { return IsCellPassable(GS, C) && !HazardCells.Contains(C); };
	auto SafePassable = [this, GS](FIntPoint C) { return IsCellPassable(GS, C) && !DangerCells.Contains(C); };

	// (a) 현재 셀이 위험 → 가장 가까운 안전셀로 도주(다른 행동 금지).
	if (DangerCells.Contains(Cur))
	{
		TArray<FIntPoint> Path;
		auto SafeGoal = [this](FIntPoint C) { return !DangerCells.Contains(C); };
		if (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, SafeGoal, Passable, Path))
		{
			State = EBotState::Flee;
			CurrentPath = MoveTemp(Path);
			PathIndex = 1;
			FleeSettleAttempts = 0;
		}
		return;
	}

	// (a-2) 도주 완주: 이미 안전셀이지만 중심에 아직 못 붙었으면 마저 중심까지 이동.
	//       위험 판정이 floored 셀 기준이라 경계를 넘는 순간 풀려, 봇이 폭발셀 경계(~50cm)에 걸친 채
	//       멈춰 있다 폭탄에 맞던 문제 방지. 중심까지 붙으면(다음 틱) 정상 행동으로 복귀.
	if (State == EBotState::Flee)
	{
		const FVector Loc = Bot->GetActorLocation();
		const FVector CurCenter = UD1BomberGridLibrary::CellToWorldCenter(Cur, Loc.Z);
		if (FVector::Dist2D(Loc, CurCenter) > FinalArrivalToleranceCm)
		{
			// 아래 (b)·(c)·(d)가 전부 이 return 밑이라, 정착에 계속 실패하면 봇이 Flee에 영구히 갇혀
			// 폭탄을 다시 못 놓는다. 한도를 두어 그 경우 Flee를 풀고 정상 판단으로 내려보낸다.
			if (++FleeSettleAttempts <= MaxFleeSettleAttempts)
			{
				CurrentPath = { Cur, Cur };
				PathIndex = 1;
				return;
			}
			UE_LOG(LogD1, Warning, TEXT("[Bot] 셀 중심 정착 %d회 실패 — Flee 해제 cell=(%d,%d) dist=%.1fcm"),
				MaxFleeSettleAttempts, Cur.X, Cur.Y, FVector::Dist2D(Loc, CurCenter));
			State = EBotState::Idle;
		}
		FleeSettleAttempts = 0;
	}

	// (b) 안전 + (소프트블록 또는 적) 인접 + 용량 여유 → 탈출 검증 후 설치.
	const int32 Capacity = PS ? PS->GetBombCapacity() : 1;
	if (OwnActiveBombCount < Capacity && (IsAdjacentToSoftBlock(GS, Cur) || IsAdjacentToEnemy(Cur)))
	{
		const int32 Range = PS ? PS->GetFirePower() : 2;
		TArray<FIntPoint> Escape;
		if (WouldSurviveBombAt(GS, Cur, Range, Escape))
		{
			Bot->GetBombPlacement()->ServerPlaceBombForAI();
			State = EBotState::Flee;
			CurrentPath = MoveTemp(Escape);
			PathIndex = 1;
			FleeSettleAttempts = 0;
			return;
		}
	}

	// (b-2) 근거리 안전 아이템이 있으면 우선 획득(확률 스킵 없이 확정 — 강한 의지). 안전 경로·근거리 한정.
	auto ItemGoal = [this, GS](FIntPoint C)
	{
		return IsCellPassable(GS, C) && !DangerCells.Contains(C) && ItemCells.Contains(C);
	};
	TArray<FIntPoint> ItemPath;
	if (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, ItemGoal, SafePassable, ItemPath)
		&& ItemPath.Num() <= MaxItemSeekCells)
	{
		State = EBotState::Seek;
		CurrentPath = MoveTemp(ItemPath);
		PathIndex = 1;
		return;
	}

	// (c) 이동: 소프트블록/적 인접 자유셀로 접근(위험셀 통과 금지). 확률적 goal 수락으로 경로 다양화.
	auto SoftGoal = [this, GS](FIntPoint C)
	{
		return IsCellPassable(GS, C) && !DangerCells.Contains(C) && IsAdjacentToSoftBlock(GS, C) && Rng.FRand() < GoalAcceptProb;
	};
	auto EnemyGoal = [this, GS](FIntPoint C)
	{
		return IsCellPassable(GS, C) && !DangerCells.Contains(C) && IsAdjacentToEnemy(C) && Rng.FRand() < GoalAcceptProb;
	};

	// aggression 높은 봇은 블록이 남아도 hunt를 먼저 시도 → 결판 가속 + 봇별 성향 차이.
	TArray<FIntPoint> Path;
	const bool bHuntFirst = Rng.FRand() < AggressionBias;
	const bool bFound = bHuntFirst
		? (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, EnemyGoal, SafePassable, Path)
			|| UD1BomberGridLibrary::FindNearestReachable(GS, Cur, SoftGoal, SafePassable, Path))
		: (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, SoftGoal, SafePassable, Path)
			|| UD1BomberGridLibrary::FindNearestReachable(GS, Cur, EnemyGoal, SafePassable, Path));
	if (bFound)
	{
		State = EBotState::Seek;
		CurrentPath = MoveTemp(Path);
		PathIndex = 1;
		return;
	}

	// (d) 목표 없음 → 정지.
	State = EBotState::Idle;
	CurrentPath.Reset();
}

void AD1BotController::SteerAlongPath(AD1BomberCharacter* Bot)
{
	if (!CurrentPath.IsValidIndex(PathIndex))
	{
		return;
	}

	const FVector Loc = Bot->GetActorLocation();
	const FVector Center = UD1BomberGridLibrary::CellToWorldCenter(CurrentPath[PathIndex], Loc.Z);
	FVector Delta = Center - Loc;
	Delta.Z = 0.f;

	// 마지막 웨이포인트는 작은 판정으로 셀 중앙에 붙임(안전셀 경계 걸침 사망 방지). 중간은 넉넉히(부드러운 이동).
	const bool bFinal = (PathIndex == CurrentPath.Num() - 1);
	const float Tol = bFinal ? FinalArrivalToleranceCm : ArrivalToleranceCm;
	if (Delta.SizeSquared() <= Tol * Tol)
	{
		// 조향만 끊으면 관성으로 20~30cm를 더 미끄러져 판정 반경(8cm)을 벗어난다 → Think의 (a-2)가
		// 다시 걸려 봇이 셀 중심을 왕복만 하며 Flee에 갇힌다. 최종 웨이포인트에선 명시적으로 정지.
		// 중간 웨이포인트는 관성을 남겨 이동이 끊기지 않게 한다.
		if (bFinal)
		{
			if (UCharacterMovementComponent* Move = Bot->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
			}
		}
		++PathIndex;
		return;
	}

	const FVector Dir = Delta.GetSafeNormal();
	// 봇 DoMove fallback: Forward→월드 +X, Right→월드 +Y (D1BomberCharacter.cpp:155-159).
	Bot->DoMove(Dir.Y, Dir.X);
}

void AD1BotController::BuildDangerMap(const AD1BomberCharacter* Bot, const AD1BomberGameState* GS)
{
	DangerCells.Reset();
	BombCells.Reset();
	EnemyCells.Reset();
	HazardCells.Reset();
	ItemCells.Reset();
	OwnActiveBombCount = 0;

	for (AD1Bomb* Bomb : TActorRange<AD1Bomb>(GetWorld()))
	{
		if (!IsValid(Bomb))
		{
			continue;
		}

		const FIntPoint Origin = UD1BomberGridLibrary::WorldToCell(Bomb->GetActorLocation());
		BombCells.Add(Origin);
		if (Bomb->GetOwner() == Bot)
		{
			++OwnActiveBombCount;
		}

		// 게임 실제 폭발과 동일 로직 → 봇이 과소/과대 회피하지 않음.
		TArray<FIntPoint> Cells, SoftHits;
		UD1BomberGridLibrary::TraceExplosionCells(GS, Origin, Bomb->GetRange(), Cells, SoftHits);
		for (const FIntPoint& C : Cells)
		{
			DangerCells.Add(C);
		}
	}

	// 이미 터진 폭발의 잔류 위험(~0.5s) — 방금 터진 셀로 경로를 새로 잡는 순간 방지.
	for (AD1ExplosionHazard* Hazard : TActorRange<AD1ExplosionHazard>(GetWorld()))
	{
		if (!IsValid(Hazard))
		{
			continue;
		}
		for (const FIntPoint& C : Hazard->GetHazardCells())
		{
			DangerCells.Add(C);
			HazardCells.Add(C);   // 이미 터져 데미지 중 — 도주 중에도 통과 금지.
		}
	}

	// 생존한 적 봇 위치(HUNT 타겟). 동적이라 통과 판정엔 넣지 않음 — 막으면 경로 jitter.
	for (AD1BomberCharacter* Char : TActorRange<AD1BomberCharacter>(GetWorld()))
	{
		if (!IsValid(Char) || Char == Bot)
		{
			continue;
		}
		const AD1BomberPlayerState* EPS = Char->GetPlayerState<AD1BomberPlayerState>();
		if (!EPS || !EPS->IsAlive())
		{
			continue;
		}
		EnemyCells.Add(UD1BomberGridLibrary::WorldToCell(Char->GetActorLocation()));
	}

	// 드롭된 파워업 위치(근거리·안전할 때만 획득 대상).
	for (AD1PowerupPickup* Pickup : TActorRange<AD1PowerupPickup>(GetWorld()))
	{
		if (!IsValid(Pickup))
		{
			continue;
		}
		ItemCells.Add(UD1BomberGridLibrary::WorldToCell(Pickup->GetActorLocation()));
	}
}

bool AD1BotController::WouldSurviveBombAt(const AD1BomberGameState* GS, const FIntPoint& Cell, int32 Range, TArray<FIntPoint>& OutEscape) const
{
	TArray<FIntPoint> Hazard, SoftHits;
	UD1BomberGridLibrary::TraceExplosionCells(GS, Cell, Range, Hazard, SoftHits);

	// 기존 위험 + 이 폭탄이 새로 만들 십자 = 최종적으로 피해야 할 셀.
	TSet<FIntPoint> Danger = DangerCells;
	Danger.Append(Hazard);
	Danger.Add(Cell);

	// 탈출 이동: 아직 안 터진 폭탄 십자는 달려서 통과 가능 — 지형·새 폭탄 셀·이미 터진 hazard만 막는다.
	auto EscPassable = [this, GS, &Cell](FIntPoint C) { return IsCellPassable(GS, C) && C != Cell && !HazardCells.Contains(C); };
	// 목표: 기존 위험에도 새 십자에도 안 걸리는 안전 셀(모퉁이 밖).
	auto EscGoal = [&Danger](FIntPoint C) { return !Danger.Contains(C); };
	if (!UD1BomberGridLibrary::FindNearestReachable(GS, Cell, EscGoal, EscPassable, OutEscape))
	{
		return false;
	}
	// 탈출이 너무 길면 도화선(체인격발로 단축 가능) 내 못 빠져나갈 수 있음 → 설치 포기.
	return OutEscape.Num() <= MaxEscapePathCells;
}

bool AD1BotController::IsCellPassable(const AD1BomberGameState* GS, const FIntPoint& Cell) const
{
	return GS && GS->IsInsideGrid(Cell) && !GS->IsWallCell(Cell)
		&& !GS->IsSoftBlockCell(Cell) && !BombCells.Contains(Cell);
}

bool AD1BotController::IsAdjacentToSoftBlock(const AD1BomberGameState* GS, const FIntPoint& Cell) const
{
	if (!GS)
	{
		return false;
	}

	for (const FIntPoint& Dir : UD1BomberGridLibrary::NeighborDirs)
	{
		if (GS->IsSoftBlockCell(Cell + Dir))
		{
			return true;
		}
	}
	return false;
}

bool AD1BotController::IsAdjacentToEnemy(const FIntPoint& Cell) const
{
	for (const FIntPoint& Dir : UD1BomberGridLibrary::NeighborDirs)
	{
		if (EnemyCells.Contains(Cell + Dir))
		{
			return true;
		}
	}
	return false;
}

void AD1BotController::EnsureSeeded(const AD1BomberPlayerState* PS)
{
	if (bSeeded)
	{
		return;
	}
	bSeeded = true;

	// 슬롯(0~3)로 봇별 안정 시드; 소수 곱으로 인접 시드 상관 완화. 슬롯 미배정이면 오브젝트 ID.
	const int32 Slot = PS ? PS->GetPlayerSlotIndex() : -1;
	const int32 Seed = (Slot >= 0) ? (7919 * (Slot + 1)) : static_cast<int32>(GetUniqueID());
	Rng.Initialize(Seed);

	ThinkIntervalSec *= Rng.FRandRange(1.f - ThinkIntervalJitter, 1.f + ThinkIntervalJitter);
	AggressionBias = Rng.FRandRange(0.3f, 0.9f);
}
