// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "D1BomberPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLivesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAliveStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerNameChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlotIndexChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpeedLevelChanged);

UCLASS()
class AD1BomberPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//~ 외부 API (서버 전용)
	bool ApplyHit();
	void SetPlayerSlotIndex(int32 NewIndex);

	/** 캡까지만 증가. */
	void AddFirePower(int32 Delta);
	void AddBombCapacity(int32 Delta);
	void AddSpeedLevel(int32 Delta);

	//~ 이벤트
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnLivesChanged OnLivesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnAliveStateChanged OnAliveStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerNameChanged OnPlayerNameChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnSlotIndexChanged OnSlotIndexChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnSpeedLevelChanged OnSpeedLevelChanged;

	//~ 복제 상태
	UPROPERTY(ReplicatedUsing = OnRep_Lives, BlueprintReadOnly, Category = "Bomber")
	int32 Lives = 3;

	UPROPERTY(ReplicatedUsing = OnRep_bIsAlive, BlueprintReadOnly, Category = "Bomber")
	bool bIsAlive = true;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 Placement = 0;

	UPROPERTY(ReplicatedUsing = OnRep_PlayerSlotIndex, BlueprintReadOnly, Category = "Bomber")
	int32 PlayerSlotIndex = -1;

	/** 폭발 범위(칸). 설치 시 폭탄에 stamp. Fire로 증가. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Powerup")
	int32 FirePower = 2;

	/** 동시 설치 가능 폭탄 수. Bomb로 증가. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Powerup")
	int32 BombCapacity = 1;

	/** 이동속도 단계. Speed로 증가 → 캐릭터 MaxWalkSpeed에 반영. */
	UPROPERTY(ReplicatedUsing = OnRep_SpeedLevel, BlueprintReadOnly, Category = "Bomber|Powerup")
	int32 SpeedLevel = 0;

	/** DS가 ?join= 토큰을 권위 roster로 해석해 설정. 결과 POST에 사용, 복제 안 함. */
	UPROPERTY(BlueprintReadOnly, Category = "Bomber")
	int64 BackendUserId = 0;

protected:
	virtual void OnRep_PlayerName() override;

	UFUNCTION()
	void OnRep_Lives();

	UFUNCTION()
	void OnRep_bIsAlive();

	UFUNCTION()
	void OnRep_PlayerSlotIndex();

	UFUNCTION()
	void OnRep_SpeedLevel();
};
