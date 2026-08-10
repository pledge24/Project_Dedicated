// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/D1MatchConfig.h"
#include "Framework/D1MatchTypes.h"
#include "GameFramework/GameModeBase.h"
#include "D1BomberGameMode.generated.h"

class AController;
class APlayerController;
class AD1PowerupPickup;
class UD1MapData;

/** 봄버맨 매치 GameMode(서버 전용) — 맵 빌드·접속 신원 검증·슬롯 배정·시작 게이트. */
UCLASS(abstract)
class AD1BomberGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	//~ Begin AGameModeBase Interface
	/** 매치 설정(식별자·토큰·명단·봇 좌석) 적재 — 접속 신원 판정보다 확실히 앞서야 한다. */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	/** GameState 컴포넌트에 매치 설정 주입. 가동은 BeginPlay(맵·봇 준비 후). */
	virtual void InitGameState() override;
	/** 재입장 거절 — 이미 kick된(다른 기기 로그인) 유저의 연결 거부. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	/** 예상 인원 다 모이면 매치 시작(시작 게이트). */
	virtual void PostLogin(APlayerController* NewPlayer) override;
	/** 점유 PlayerStart 해제 — fallback 경로 슬롯 누수 방지. */
	virtual void Logout(AController* Exiting) override;
	//~ End AGameModeBase Interface

protected:
	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	//~ End AActor Interface

	//~ Begin AGameModeBase Interface
	/** Login 통과후 해당 클라가 초대받은 손님인지 토큰으로 판단. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	/** PostLogin 시점에서 미사용 PlayerStart 랜덤 선택 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	//~ End AGameModeBase Interface

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
	/**
	 * 매치 시작 후 들어오는 접속인가 — PreLogin·InitNewPlayer 공용 판정.
	 * 토큰 없는 PIE/standalone은 시작 게이트 없이 즉시 Playing이 되므로 항상 false(입장 허용).
	 */
	bool IsJoinAfterMatchStart() const;

	/** InitGame에서 FD1MatchConfig::Load()로 적재 — 매치 식별자/토큰/명단/봇 좌석. */
	FD1MatchConfig MatchConfig;

//~ 슬롯 배정
private:
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

//~ 봇 (봇전 서버측 스폰)
private:
	/** 봇전: -Bots= 로 주입된 봇 좌석을 서버측 스폰(AI 빙의, 가만히 서 있음). 맵 빌드 후·시작 게이트 전 호출. */
	void SpawnBots();

	/** 봇 컨트롤러 클래스(기본 AD1BotController, 생성자 지정). PlayerState를 얻어 PlayerArray에 편입. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AController> BotControllerClass;

//~ 시작 게이트·매치 흐름
private:
	/** 시작 게이트 대기 상한(초). 안 차도 이 시간 뒤 시작. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float WaitForPlayersTimeoutSec = 20.f;

	/** 매치 종료 후 이 시간 뒤 DS 강제 종료(하드캡). 클라 복귀보다 길게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float ShutdownGraceSec = 30.f;
};
