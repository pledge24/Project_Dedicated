// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "D1BotController.generated.h"

class AD1BomberCharacter;
class AD1BomberGameState;
class AD1BomberPlayerState;

/** 봇 사고 상태(서버 전용). */
enum class EBotState : uint8
{
	/** 도달 가능한 타겟 없음 — 정지. */
	Idle,
	/** 소프트블록 인접 자유셀로 접근 중. */
	Seek,
	/** 위험 회피 중 — 안전셀로 도주. */
	Flee,
};

/** 봇전(Bot-Fill) 봇 컨트롤러(서버 전용). PlayerState를 얻어 PlayerArray에 편입 — 시작 게이트·승패·카드·결과가 전부 PlayerArray 기준.
 *  단순 그리드 AI: 폭탄 위험 회피 + 소프트블록 폭파 + 탈출. 사고는 저빈도(누산기), 조향은 매 프레임. */
UCLASS()
class AD1BotController : public AAIController
{
	GENERATED_BODY()

public:
	AD1BotController();

	//~ Begin AActor Interface
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

//~ 봇 AI
protected:
	/** 저빈도 사고: 위험맵 구축·상태 전이·경로 재계산·폭탄 결정. */
	void Think(AD1BomberCharacter* Bot, const AD1BomberGameState* GS, const AD1BomberPlayerState* PS);

	/** 매 프레임 조향: 현재 경로의 다음 셀 중심으로 DoMove. */
	void SteerAlongPath(AD1BomberCharacter* Bot);

private:
	/** 활성 폭탄·잔류 위험 스캔 → DangerCells/BombCells/OwnActiveBombCount 갱신. */
	void BuildDangerMap(const AD1BomberCharacter* Bot, const AD1BomberGameState* GS);

	/** Cell에 폭탄을 놓아도 안전셀로 탈출 가능한지(가상 위험맵 BFS). 성공 시 OutEscape 채움. */
	bool WouldSurviveBombAt(const AD1BomberGameState* GS, const FIntPoint& Cell, int32 Range, TArray<FIntPoint>& OutEscape) const;

	bool IsCellPassable(const AD1BomberGameState* GS, const FIntPoint& Cell) const;
	bool IsAdjacentToSoftBlock(const AD1BomberGameState* GS, const FIntPoint& Cell) const;

	/** d1.BotDebug 켜지면 위험셀(빨강)·경로(초록)·현재 웨이포인트(노랑)·상태명을 서버 월드에 그림(=PIE 뷰포트). */
	void DrawDebug(const AD1BomberCharacter* Bot) const;

	/** 사고 주기(초). 도화선 3s·잔류 0.5s 대비 충분한 반응성, 매 프레임 부담 회피. */
	float ThinkIntervalSec = 0.15f;

	/** 셀 중심 도달 판정 반경(cm). 봄버맨 자유이동이라 정확 중심 불필요. */
	float ArrivalToleranceCm = 20.f;

	EBotState State = EBotState::Idle;
	TArray<FIntPoint> CurrentPath;
	int32 PathIndex = 0;
	float ThinkAccumulatorSec = 0.f;
	TSet<FIntPoint> DangerCells;
	TSet<FIntPoint> BombCells;
	int32 OwnActiveBombCount = 0;
};
