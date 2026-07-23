// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BotController.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

#include "Core/D1LogChannels.h"
#include "Game/Character/D1BomberCharacter.h"
#include "Game/D1Bomb.h"
#include "Game/D1ExplosionHazard.h"
#include "Game/D1BomberGridLibrary.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"

namespace
{
	const FIntPoint BotNeighborDirs[4] = {
		FIntPoint( 1,  0),
		FIntPoint(-1,  0),
		FIntPoint( 0,  1),
		FIntPoint( 0, -1)
	};

	TAutoConsoleVariable<int32> CVarBotDebug(
		TEXT("d1.BotDebug"),
		0,
		TEXT("봇 AI 디버그 시각화(위험셀/경로/상태). 0=off, 1=on."),
		ECVF_Default);
}

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
		DrawDebug(Bot);
	}
	SteerAlongPath(Bot);
}

void AD1BotController::Think(AD1BomberCharacter* Bot, const AD1BomberGameState* GS, const AD1BomberPlayerState* PS)
{
	BuildDangerMap(Bot, GS);

	const FIntPoint Cur = UD1BomberGridLibrary::WorldToCell(Bot->GetActorLocation());
	auto Passable = [this, GS](FIntPoint C) { return IsCellPassable(GS, C); };

	// (a) 현재 셀이 위험 → 회피 최우선(다른 행동 금지).
	if (DangerCells.Contains(Cur))
	{
		TArray<FIntPoint> Path;
		auto SafeGoal = [this](FIntPoint C) { return !DangerCells.Contains(C); };
		if (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, SafeGoal, Passable, Path))
		{
			State = EBotState::Flee;
			CurrentPath = MoveTemp(Path);
			PathIndex = 1;
		}
		return;
	}

	// (b) 안전 + 소프트블록 인접 + 용량 여유 → 탈출 검증 후 설치.
	const int32 Capacity = PS ? PS->GetBombCapacity() : 1;
	if (OwnActiveBombCount < Capacity && IsAdjacentToSoftBlock(GS, Cur))
	{
		const int32 Range = PS ? PS->GetFirePower() : 2;
		TArray<FIntPoint> Escape;
		if (WouldSurviveBombAt(GS, Cur, Range, Escape))
		{
			Bot->ServerPlaceBombForAI();
			State = EBotState::Flee;
			CurrentPath = MoveTemp(Escape);
			PathIndex = 1;
			return;
		}
	}

	// (c) 가장 가까운 소프트블록 인접 자유셀로 접근.
	TArray<FIntPoint> Path;
	auto NextToSoft = [this, GS](FIntPoint C) { return IsCellPassable(GS, C) && IsAdjacentToSoftBlock(GS, C); };
	if (UD1BomberGridLibrary::FindNearestReachable(GS, Cur, NextToSoft, Passable, Path))
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

	if (Delta.SizeSquared() <= ArrivalToleranceCm * ArrivalToleranceCm)
	{
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
		}
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

	// 탈출 이동: 폭발 예정 셀은 "아직 안 터진" 통로라 달려서 통과 가능 — 지형(벽/블록/기존폭탄)과 새 폭탄 셀만 막는다.
	auto EscPassable = [this, GS, &Cell](FIntPoint C) { return IsCellPassable(GS, C) && C != Cell; };
	// 목표: 기존 위험에도 새 십자에도 안 걸리는 안전 셀(모퉁이 밖).
	auto EscGoal = [&Danger](FIntPoint C) { return !Danger.Contains(C); };
	return UD1BomberGridLibrary::FindNearestReachable(GS, Cell, EscGoal, EscPassable, OutEscape);
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

	for (const FIntPoint& Dir : BotNeighborDirs)
	{
		if (GS->IsSoftBlockCell(Cell + Dir))
		{
			return true;
		}
	}
	return false;
}

void AD1BotController::DrawDebug(const AD1BomberCharacter* Bot) const
{
	if (CVarBotDebug.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Bot)
	{
		return;
	}

	const float Z = Bot->GetActorLocation().Z;
	// 사고 주기보다 약간 길게 유지 → 매 사고마다 갱신되며 깜빡임 없음.
	const float Life = ThinkIntervalSec * 1.5f;

	for (const FIntPoint& C : DangerCells)
	{
		DrawDebugBox(World, UD1BomberGridLibrary::CellToWorldCenter(C, Z), FVector(45.f, 45.f, 20.f), FColor::Red, false, Life, 0, 2.f);
	}
	for (const FIntPoint& C : CurrentPath)
	{
		DrawDebugBox(World, UD1BomberGridLibrary::CellToWorldCenter(C, Z), FVector(28.f, 28.f, 28.f), FColor::Green, false, Life, 0, 2.f);
	}
	if (CurrentPath.IsValidIndex(PathIndex))
	{
		DrawDebugSphere(World, UD1BomberGridLibrary::CellToWorldCenter(CurrentPath[PathIndex], Z), 25.f, 8, FColor::Yellow, false, Life);
	}

	const TCHAR* StateLabel = TEXT("Idle");
	switch (State)
	{
	case EBotState::Seek:
		StateLabel = TEXT("Seek");
		break;
	case EBotState::Flee:
		StateLabel = TEXT("Flee");
		break;
	default:
		break;
	}
	DrawDebugString(World, Bot->GetActorLocation() + FVector(0.f, 0.f, 120.f),
		FString::Printf(TEXT("%s (bombs %d)"), StateLabel, OwnActiveBombCount), nullptr, FColor::White, Life, false);
}
