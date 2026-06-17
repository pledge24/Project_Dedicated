// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "D1BomberGameMode.generated.h"

class APlayerController;
class AD1BomberPlayerState;
class AD1PowerupPickup;
class AD1WallBlock;
class UD1MapData;
enum class EBomberEndReason : uint8;

/** -Roster= 로 주입된 입장 토큰 → 권위 신원(userId) 매핑. 좌석은 DS가 입장 시 랜덤 배정. */
struct FD1JoinEntry
{
	int64 UserId = 0;
};

UCLASS(abstract)
class AD1BomberGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** travel ?join= 토큰을 권위 roster로 해석해 userId 확정(서버권위). 좌석은 ChoosePlayerStart가 랜덤 배정. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	/** 점유 PlayerStart 해제 — fallback 경로 슬롯 누수 방지. */
	virtual void Logout(AController* Exiting) override;

	/** 예상 인원 다 모이면 매치 시작(시작 게이트). */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** 서버 전용: 사망 등록·등수 부여, 1명 남으면 매치 종료. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);
	void EnsureAliveListInitialized();

	/** Waiting→Playing + 매치 타이머 시작. 한 번만 실행(가드). */
	void StartMatch();

	/** 대기 타임아웃 → 현재 인원으로 매치 시작. */
	void OnWaitForPlayersTimeout();

	/** 매치 시간 만료 → 종료. placement 룰은 v2. */
	void OnMatchTimeExpired();

	/** 빌드할 맵 데이터. -MapData= 로 오버라이드 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UD1MapData> MapData;

	/** 블록 스폰 Z. 100cm 큐브가 바닥에 앉는 높이=50. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float BlockZ = 50.f;

	/** 드롭할 파워업 픽업 BP. 미지정이면 드롭 안 함. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AD1PowerupPickup> PowerupPickupClass;

	/** 소프트블록 1개 파괴당 드롭 확률(0~1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
	float PowerupDropChance = 0.3f;

	/** 드롭 시 Fire/Bomb/Speed 가중치. 합이 0이면 드롭 안 함. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 FireDropWeight = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 BombDropWeight = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 SpeedDropWeight = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	float PowerupZ = 40.f;

	/** 시작 게이트 대기 상한(초). 안 차도 이 시간 뒤 시작. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float WaitForPlayersTimeoutSec = 20.f;

	/** 매치 종료 후 이 시간 뒤 DS 강제 종료(하드캡). 클라 복귀보다 길게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float ShutdownGraceSec = 30.f;

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	bool bMatchEnded = false;

	bool bMatchStarted = false;

	FTimerHandle MatchTimerHandle;

	FTimerHandle WaitForPlayersTimerHandle;

	/** -ExpectedPlayers= 로 주입. 이 수만큼 접속 시 매치 시작(0/1=즉시). */
	int32 ExpectedPlayerCount = 0;

	/** -MatchId/-MatchToken 으로 주입. 결과 POST 인증용(비면 스킵). */
	FString CurrentMatchId;
	FString CurrentMatchToken;

	/** -Roster= 로 주입(token→userId). InitNewPlayer가 ?join=로 신원 매핑. */
	TMap<FString, FD1JoinEntry> JoinRoster;
};
