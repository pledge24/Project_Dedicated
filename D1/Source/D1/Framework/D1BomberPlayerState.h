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

/** 플레이어 복제 상태 — 생명·슬롯·파워업·등수·백엔드 신원. */
UCLASS()
class AD1BomberPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	//~ Begin AActor Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

protected:
	//~ Begin APlayerState Interface
	virtual void OnRep_PlayerName() override;
	//~ End APlayerState Interface

//~ 생명·사망
public:
	/** 서버 전용. 사망 전환 시 true 반환. */
	bool ApplyHit();
	bool IsAlive() const { return bIsAlive; }
	int32 GetLives() const { return Lives; }

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnLivesChanged OnLivesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnAliveStateChanged OnAliveStateChanged;

protected:
	UFUNCTION()
	void OnRep_Lives();

	UFUNCTION()
	void OnRep_bIsAlive();

private:
	UPROPERTY(ReplicatedUsing = OnRep_Lives, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	int32 Lives = 3;

	UPROPERTY(ReplicatedUsing = OnRep_bIsAlive, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	bool bIsAlive = true;

//~ 슬롯 배정
public:
	/** 서버 전용. */
	void SetPlayerSlotIndex(int32 NewIndex);
	int32 GetPlayerSlotIndex() const { return PlayerSlotIndex; }

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnSlotIndexChanged OnSlotIndexChanged;

protected:
	UFUNCTION()
	void OnRep_PlayerSlotIndex();

private:
	/** 좌석 슬롯 0~3, -1=미배정. */
	UPROPERTY(ReplicatedUsing = OnRep_PlayerSlotIndex, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	int32 PlayerSlotIndex = -1;

//~ 파워업 (화력·폭탄·속도)
public:
	/** 서버 전용. 캡까지만 증가. */
	void AddFirePower(int32 Delta);
	void AddBombCapacity(int32 Delta);
	void AddSpeedLevel(int32 Delta);
	int32 GetFirePower() const { return FirePower; }
	int32 GetBombCapacity() const { return BombCapacity; }
	int32 GetSpeedLevel() const { return SpeedLevel; }

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnSpeedLevelChanged OnSpeedLevelChanged;

protected:
	UFUNCTION()
	void OnRep_SpeedLevel();

private:
	/** 폭발 범위(칸). 설치 시 폭탄에 stamp. Fire로 증가. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	int32 FirePower = 2;

	/** 동시 설치 가능 폭탄 수. Bomb로 증가. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	int32 BombCapacity = 1;

	/** 이동속도 단계. Speed로 증가 → 캐릭터 MaxWalkSpeed에 반영. */
	UPROPERTY(ReplicatedUsing = OnRep_SpeedLevel, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	int32 SpeedLevel = 0;

//~ 등수
public:
	/** 서버 전용. */
	void SetPlacement(int32 NewPlacement);
	int32 GetPlacement() const { return Placement; }

private:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	int32 Placement = 0;

//~ 플레이어 이름
public:
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerNameChanged OnPlayerNameChanged;

//~ 백엔드 신원
public:
	/** 서버 전용. */
	void SetBackendUserId(int64 NewUserId);
	int64 GetBackendUserId() const { return BackendUserId; }

private:
	/** DS가 ?join= 토큰을 권위 roster로 해석해 설정. 결과 POST에 사용, 복제 안 함. */
	UPROPERTY(BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	int64 BackendUserId = 0;
};
