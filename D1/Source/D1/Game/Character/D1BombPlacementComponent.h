// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "D1BombPlacementComponent.generated.h"

class AD1Bomb;
class AD1BomberCharacter;
class AD1BomberPlayerState;

/** 폭탄 설치·추적 전담 컴포넌트(캐릭터 부착, 서버 권위 검증 + 양측 통과 무시 추적). */
UCLASS()
class UD1BombPlacementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UD1BombPlacementComponent();

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 설치 입력 진입점(캐릭터 입력 바인딩 대상). 서버가 CanPlaceBombAt로 전 조건 재검증. */
	UFUNCTION(Server, Reliable)
	void ServerTryPlaceBomb();

	/** 서버 전용: 봇 AI의 폭탄 설치 요청. RPC 왕복 없이 동일 검증 재사용. */
	void ServerPlaceBombForAI();

	/** 폭탄이 터지면서 호출 — 소유자 슬롯 회수. */
	void NotifyBombDestroyed(AD1Bomb* Bomb);

	/** 폭탄 스폰 시 겹친 캐릭터의 통과 허용 등록. 영역 이탈 해제는 Tick이 담당. */
	void AddIgnoredBomb(AD1Bomb* Bomb);

private:
	/** 폭탄 설치 사전조건 전부(스턴·용량·페이즈·생존·격자·벽·중복셀). 서버 RPC 검증부. */
	bool CanPlaceBombAt(const FIntPoint& Cell, AD1BomberPlayerState* PS);

	/** 서버 전용: 죽은 weak ptr 정리 후 활성 폭탄 수 반환. */
	int32 GetActiveBombCount();

	void UpdateIgnoredBombs();

	AD1BomberCharacter* GetBomberOwner() const;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1Bomb> BombClass;

	TArray<TWeakObjectPtr<AD1Bomb>> ActiveBombs;

	TSet<TWeakObjectPtr<AD1Bomb>> IgnoredBombs;
};
