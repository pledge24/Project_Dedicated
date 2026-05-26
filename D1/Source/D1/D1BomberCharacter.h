// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "D1BomberCharacter.generated.h"

class AD1Bomb;
class AD1BomberPlayerState;
class UInputAction;
class UInputComponent;
struct FInputActionValue;

UCLASS(abstract)
class AD1BomberCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AD1BomberCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	/** 탑다운: 입력을 카메라 yaw 축으로 투영. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMove(float Right, float Forward);

	bool IsInvulnerable() const { return bIsInvulnerable; }

	/** 사망 시 메시/콜리전/이동 정리. 서버·클라 모두에서 안전 (PS OnRep 경유로 양쪽에서 호출). */
	void HandleDeath();

	/** ---------------------
	 *		Server Only
	 * ---------------------*/
	void StartInvulnerability(float Duration);
	void NotifyBombDestroyed(AD1Bomb* Bomb);
	void AddIgnoredBomb(AD1Bomb* Bomb);

protected:
	UFUNCTION(Server, Reliable)
	void ServerTryPlaceBomb();

	UFUNCTION()
	void OnRep_Invulnerable();

	/** PlayerState의 OnAliveStateChanged 바인딩 핸들러. 클라까지 사망 정리 전파. */
	UFUNCTION()
	void OnPlayerAliveStateChanged();

	/** PS의 OnPlayerNameChanged 바인딩 핸들러. 이름표 재푸시 위해 OnPlayerStateReady 재호출. */
	UFUNCTION()
	void OnPlayerNameRefreshed();

	/** PS가 (서버·클라 모두) 처음 확보된 시점 + 이름이 갱신될 때마다 호출. BP에서 이름표 등 UI 푸시용. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bomber|Events", meta = (DisplayName = "On Player State Ready"))
	void OnPlayerStateReady();

	void OnMoveInput(const FInputActionValue& Value);

	/** 서버 전용: 죽은 weak ptr 정리 후 활성 폭탄 수 반환. */
	int32 GetActiveBombCount();

	UPROPERTY()
	TArray<TWeakObjectPtr<AD1Bomb>> ActiveBombs;

	UPROPERTY()
	TSet<TWeakObjectPtr<AD1Bomb>> IgnoredBombs;

private:
	void EndInvulnerability();
	void TickBlink();
	void UpdateIgnoredBombs();

	/** PossessedBy / OnRep_PlayerState 양쪽에서 호출. PS 확보되면 OnAliveStateChanged 바인딩. */
	void RefreshPlayerStateBinding();

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> PlaceBombAction;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1Bomb> BombClass;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber", meta = (ClampMin = "1"))
	int32 MaxBombCount = 1;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	bool bIsInvulnerable;

	FTimerHandle InvulnTimerHandle;
	FTimerHandle BlinkTimerHandle;
	bool bBlinkVisible;

	/** 현재 바인딩된 PlayerState. 재바인딩 시 중복 방지/이전 핸들러 제거용. */
	TWeakObjectPtr<AD1BomberPlayerState> BoundPlayerState;
};
