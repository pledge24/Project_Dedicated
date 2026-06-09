// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1GameMode.h"
#include "D1BomberGameMode.generated.h"

class APlayerController;
class AD1BomberPlayerState;
class AD1WallBlock;
enum class EBomberEndReason : uint8;

UCLASS(abstract)
class AD1BomberGameMode : public AD1GameMode
{
	GENERATED_BODY()

public:
	AD1BomberGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** 접속 시 travel URL의 ?userId= 를 PlayerState에 저장(서버 결과 POST용). */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

	/** 서버 전용: 플레이어 사망 등록, 등수 부여, 1명 남으면 매치 종료. */
	void NotifyPlayerDied(AD1BomberPlayerState* DeadPS);

private:
	void PopulateWallData();
	void EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason);
	void EnsureAliveListInitialized();

	/** 매치 시간 만료 → 매치 종료. placement 룰은 v2에서 정의. */
	void OnMatchTimeExpired();

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> UsedStarts;

	UPROPERTY()
	TArray<TObjectPtr<AD1BomberPlayerState>> AlivePlayerStates;

	bool bMatchEnded = false;

	/** 매치 제한시간 만료 콜백용 타이머. */
	FTimerHandle MatchTimerHandle;

	/** 백엔드가 spawn 시 -MatchId/-MatchToken 으로 주입. 결과 POST 인증에 사용(비면 스킵). */
	FString CurrentMatchId;
	FString CurrentMatchToken;
};
