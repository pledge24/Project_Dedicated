// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "D1BomberGameMode.generated.h"

class APlayerController;
class AD1PowerupPickup;
class UD1MapData;

/** -Roster= 로 주입된 입장 토큰 → 권위 신원(userId) 매핑. 좌석은 DS가 입장 시 랜덤 배정. */
struct FD1JoinEntry
{
	int64 UserId = 0;
};

UCLASS(abstract)
class AD1BomberGameMode : public AGameModeBase
{
	GENERATED_BODY()

//~ 공통
public:
	AD1BomberGameMode();

	virtual void BeginPlay() override;

	/** Login 통과후 해당 클라가 초대받은 손님인지 토큰으로 판단. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	/** PostLogin 시점에서 미사용 PlayerStart 랜덤 선택 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** 예상 인원 다 모이면 매치 시작(시작 게이트). */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** 점유 PlayerStart 해제 — fallback 경로 슬롯 누수 방지. */
	virtual void Logout(AController* Exiting) override;

//~ 맵 빌드
private:
	/** 빌드할 맵 데이터. -MapData= 로 오버라이드 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UD1MapData> MapData;

	/** 블록 스폰 Z. 100cm 큐브가 바닥에 앉는 높이=50. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float BlockZ = 50.f;

//~ 파워업 드롭
private:
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

//~ 인증 — 접속 신원 검증
private:
	/** -MatchId/-MatchToken 으로 주입. 결과 POST 인증용(비면 스킵). */
	FString CurrentMatchId;
	FString CurrentMatchToken;

	/** -Roster= 로 주입(token→userId). InitNewPlayer가 ?join=로 신원 매핑. */
	TMap<FString, FD1JoinEntry> JoinRoster;

//~ 슬롯 배정
private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

//~ 시작 게이트·매치 흐름
private:
	/** 시작 게이트 대기 상한(초). 안 차도 이 시간 뒤 시작. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float WaitForPlayersTimeoutSec = 20.f;

	/** 매치 종료 후 이 시간 뒤 DS 강제 종료(하드캡). 클라 복귀보다 길게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float ShutdownGraceSec = 30.f;

	/** -ExpectedPlayers= 로 주입. 매치 흐름 컴포넌트에 전달할 시작 정원(0/1=즉시). */
	int32 ExpectedPlayerCount = 0;
};
