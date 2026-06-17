// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "D1BomberCharacter.generated.h"

class AD1Bomb;
class AD1BomberPlayerState;
class UAnimMontage;
class UAnimSequenceBase;
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
	float GetHitInvulnSeconds() const { return HitInvulnSeconds; }

	/** 사망 시 메시/콜리전/이동 정리. 서버·클라 양쪽서 호출돼도 안전. */
	void HandleDeath();

	//~ 서버 전용
	void StartInvulnerability(float Duration);

	/** 서버 전용: 피격 경직 시작. StunDuration 동안 입력 차단. */
	void ApplyHitStun();

	void NotifyBombDestroyed(AD1Bomb* Bomb);
	void AddIgnoredBomb(AD1Bomb* Bomb);

protected:
	UFUNCTION(Server, Reliable)
	void ServerTryPlaceBomb();

	UFUNCTION()
	void OnRep_Invulnerable();

	/** PS OnAliveStateChanged 핸들러. 클라까지 사망 정리 전파. */
	UFUNCTION()
	void OnPlayerAliveStateChanged();

	/** PS OnPlayerNameChanged 핸들러. 이름표 재푸시 위해 OnPlayerStateReady 재호출. */
	UFUNCTION()
	void OnPlayerNameRefreshed();

	/** PS OnSpeedLevelChanged 핸들러. SpeedLevel을 MaxWalkSpeed에 반영. */
	UFUNCTION()
	void OnSpeedLevelChanged();

	/** PS 최초 확보 + 이름 갱신마다 호출. BP에서 이름표 등 UI 푸시용. */
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

	/** PossessedBy/OnRep_PlayerState 양쪽서 호출. PS 확보 시 OnAliveStateChanged 바인딩. */
	void RefreshPlayerStateBinding();

	/** 사망 연출 종료 후 메시 숨김. 타이머 콜백. */
	void FinishDeath();

	/** 경직 해제. 타이머 콜백(서버). */
	void EndStun();

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> PlaceBombAction;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1Bomb> BombClass;

	/** 사망 시 재생 몽타주. Auto Blend Out=off 권장. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** 기본 이동속도(SpeedLevel 0). 생성자에서 MaxWalkSpeed에 적용. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BaseWalkSpeed = 500.f;

	/** Speed 아이템 1단계당 가산 속도. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float SpeedStep = 60.f;

	/** 사망 애니 종료 후 메시 숨김까지 추가 대기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float DeathHideDelay = 1.0f;

	/** 피격 시 재생 애니. DefaultSlot 동적 몽타주. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimSequenceBase> HitAnim;

	/** 피격 경직(조작 불가) 지속 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float StunDuration = 1.0f;

	/** 비치명 피격 후 무적 지속(초). 폭탄이 이 값으로 부여. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float HitInvulnSeconds = 2.f;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	bool bIsInvulnerable;

	UPROPERTY(Replicated)
	bool bStunned = false;

	FTimerHandle InvulnTimerHandle;
	FTimerHandle BlinkTimerHandle;
	FTimerHandle DeathHideTimerHandle;
	FTimerHandle StunTimerHandle;
	bool bBlinkVisible;
	bool bDeathHandled = false;

	/** 재바인딩 시 중복 방지·이전 핸들러 제거용. */
	TWeakObjectPtr<AD1BomberPlayerState> BoundPlayerState;
};
