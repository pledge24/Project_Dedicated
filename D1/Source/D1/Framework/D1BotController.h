// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Math/RandomStream.h"
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
	bool IsAdjacentToEnemy(const FIntPoint& Cell) const;

	/** 첫 Think에서 1회: 봇별 난수 시드 → 사고주기 지터·공격성 편향(움직임 다양성). */
	void EnsureSeeded(const AD1BomberPlayerState* PS);

	/** 사고 주기(초). 도화선 3s·잔류 0.5s 대비 충분한 반응성, 매 프레임 부담 회피. 봇별 지터 적용 전 기준값. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	float ThinkIntervalSec = 0.15f;

	/** 중간 웨이포인트 도착 판정 반경(cm). 넉넉히 잡아 부드러운 이동. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	float ArrivalToleranceCm = 20.f;

	/** 경로 최종 목적지 도착 판정 반경(cm). 작게 잡아 안전셀 중앙에 붙게 → 폭발 경계 걸침 사망 방지. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	float FinalArrivalToleranceCm = 8.f;

	/** 봇별 사고주기 지터 비율(±). lockstep 군집 이동 해소. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	float ThinkIntervalJitter = 0.2f;

	/** SEEK/HUNT goal 확률적 수락(최근접 대신 차선 정착 → 경로 다양성). 낮을수록 우회 잦음. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	float GoalAcceptProb = 0.65f;

	/** 폭탄 설치 허용 탈출 경로 최대 길이(셀). 클수록 과감(더 긴 탈출도 감수). */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	int32 MaxEscapePathCells = 8;

	/** 아이템 추적 최대 거리(셀). 이보다 멀면 무시 — 멀리 있는 아이템 추격 방지. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	int32 MaxItemSeekCells = 5;

	/** 도주 정착 재시도 한도. 넘으면 Flee를 풀어 (b)·(c)로 내려보낸다 — 무한 래치 방지. */
	UPROPERTY(EditDefaultsOnly, Category = "Bot")
	int32 MaxFleeSettleAttempts = 6;

	EBotState State = EBotState::Idle;
	TArray<FIntPoint> CurrentPath;
	int32 PathIndex = 0;
	float ThinkAccumulatorSec = 0.f;
	/** 지터 적용된 실제 사고 주기(EnsureSeeded에서 1회 산출) — 설정값(ThinkIntervalSec)은 덮어쓰지 않는다. */
	float EffectiveThinkIntervalSec = 0.15f;
	/** 연속 도주 정착 시도 횟수(MaxFleeSettleAttempts 대조용). */
	int32 FleeSettleAttempts = 0;
	TSet<FIntPoint> DangerCells;
	TSet<FIntPoint> BombCells;
	TSet<FIntPoint> EnemyCells;
	TSet<FIntPoint> HazardCells;
	TSet<FIntPoint> ItemCells;
	/** 이 봇이 설치해 아직 안 터진 폭탄 수 — 용량(GetBombCapacity) 초과 설치 차단용. */
	int32 OwnActiveBombCount = 0;

	/** 봇별 개성(EnsureSeeded에서 1회 설정). */
	FRandomStream Rng;
	float AggressionBias = 0.5f;
	bool bSeeded = false;
};
