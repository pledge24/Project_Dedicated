// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1GameMode.h"
#include "D1BomberGameMode.generated.h"

class APlayerController;
class AD1BomberPlayerState;
class AD1PowerupPickup;
class AD1WallBlock;
class UD1MapData;
enum class EBomberEndReason : uint8;

UCLASS(abstract)
class AD1BomberGameMode : public AD1GameMode
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** 접속 시 travel URL의 ?userId=/?slot= 를 PlayerState에 저장(서버 결과 POST·권위 슬롯용). */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	/** 퇴장 시 점유 PlayerStart 해제 — fallback(순번) 경로 슬롯 누수 방지. */
	virtual void Logout(AController* Exiting) override;

	/** 입장 완료 시점: 예상 인원 다 모이면 매치 시작(시작 게이트). */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** 서버 전용: 플레이어 사망 등록, 등수 부여, 1명 남으면 매치 종료. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

	/** 서버 전용: 소프트블록 파괴 자리에 확률·가중표로 파워업 드롭. SoftBlock이 호출. */
	void TrySpawnPowerupAt(const FIntPoint& Cell);

private:
	/** 서버 전용: MapData(ASCII)를 파싱해 그리드/벽/소프트블록/스폰/바닥을 런타임 스폰 + GameState 채움. */
	void BuildMapFromData();

	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);
	void EnsureAliveListInitialized();

	/** Waiting → Playing 전환 + 매치 타이머 시작. 한 번만 실행(가드). */
	void StartMatch();

	/** 예상 인원 미달 상태로 대기 타임아웃 → 현재 인원으로 매치 시작. */
	void OnWaitForPlayersTimeout();

	/** 매치 시간 만료 → 매치 종료. placement 룰은 v2에서 정의. */
	void OnMatchTimeExpired();

	/** 매치 종료 후 DS 자가 종료 감시 시작(DS 전용). 전원 퇴장 또는 하드캡 시 종료. */
	void StartShutdownWatchdog();

	/** 1초마다 인원 확인 — 0명이거나 하드캡 도달 시 DS 종료 요청. */
	void TickShutdownWatchdog();

	/** DS 프로세스 종료 요청(RequestExit). 백엔드가 포트/슬롯 자동 회수. */
	void RequestServerShutdown();

	/** 이 매치에서 빌드할 맵 데이터(BP 기본값). `-MapData=` 커맨드라인으로 오버라이드 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UD1MapData> MapData;

	/** 블록 스폰 Z(셀 중심). 100cm 큐브가 바닥(Z=0)에 앉는 높이 = 50. */
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

	/** 파워업 스폰 높이(셀 중심 Z). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Powerup", meta = (AllowPrivateAccess = "true"))
	float PowerupZ = 40.f;

	/** 시작 게이트 대기 상한(초). 예상 인원이 안 차도 이 시간 뒤엔 시작. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float WaitForPlayersTimeoutSec = 20.f;

	/** 매치 종료 후 전원 퇴장이 없어도 이 시간 뒤엔 DS 강제 종료(하드캡). 클라 복귀 카운트다운보다 길게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bomber|Match", meta = (AllowPrivateAccess = "true"))
	float ShutdownGraceSec = 30.f;

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	bool bMatchEnded = false;

	bool bMatchStarted = false;

	/** 매치 제한시간 만료 콜백용 타이머. */
	FTimerHandle MatchTimerHandle;

	/** 시작 게이트 대기 타임아웃용 타이머. */
	FTimerHandle WaitForPlayersTimerHandle;

	/** 매치 종료 후 종료 감시용 타이머. */
	FTimerHandle ShutdownWatchdogHandle;

	/** 종료 감시 누적 경과(초). ShutdownGraceSec 도달 시 하드캡 종료. */
	float ShutdownElapsed = 0.f;

	/** 백엔드가 spawn 시 -ExpectedPlayers= 로 주입. 이 수만큼 접속하면 매치 시작(0/1=즉시). */
	int32 ExpectedPlayerCount = 0;

	/** 백엔드가 spawn 시 -MatchId/-MatchToken 으로 주입. 결과 POST 인증에 사용(비면 스킵). */
	FString CurrentMatchId;
	FString CurrentMatchToken;
};
