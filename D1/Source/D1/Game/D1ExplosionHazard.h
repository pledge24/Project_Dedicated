// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1ExplosionHazard.generated.h"

/** 폭발 십자 셀에 머무는 서버 전용 위험 영역.
 *  불꽃 수명 동안 셀을 주기적으로 재스윕해 뒤늦게 들어온 캐릭터도 피격한다.
 *  비주얼은 AD1ExplosionFX가 별도 담당(이 액터는 보이지 않음). */
UCLASS()
class AD1ExplosionHazard : public AActor
{
	GENERATED_BODY()

public:
	AD1ExplosionHazard();

	/** 서버 전용: 셀 목록·지속시간으로 위험 영역 가동(즉시 1차 스윕 + 반복 타이머). */
	void Initialize(const TArray<FIntPoint>& InCells, float InDurationSec);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TickHazard();
	void ApplyExplosionDamage();

	/** 반복 스윕 간격(초). 셀(100cm)을 한 틱에 못 건너뛸 만큼 촘촘하게. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float HazardSweepInterval = 0.05f;

	/** 피격 박스: 셀보다 약간 작은 가로(45) + 캐릭터 높이(80). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	FVector ExplosionHitExtent = FVector(45.f, 45.f, 80.f);

	TArray<FIntPoint> HazardCells;
	float HazardDurationSec = 0.5f;
	float ElapsedSec = 0.f;
	FTimerHandle HazardSweepTimerHandle;
};
