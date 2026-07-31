// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "D1BomberCharacter.generated.h"

class AD1BomberPlayerState;
class UD1BombPlacementComponent;
class UD1BomberCosmeticComponent;
class UInputAction;
class UInputComponent;
struct FInputActionValue;

/** 봄버 캐릭터 — 이동·피격/무적/스턴 권위 상태. 폭탄 설치·연출은 전담 컴포넌트에 위임. */
UCLASS(abstract)
class AD1BomberCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AD1BomberCharacter(const FObjectInitializer& ObjectInitializer);

	//~ Begin AActor Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

	//~ Begin APawn Interface
	virtual void PossessedBy(AController* NewController) override;
	virtual void Restart() override;
	//~ End APawn Interface

protected:
	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	//~ End AActor Interface

	//~ Begin APawn Interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void OnRep_Controller() override;
	//~ End APawn Interface

//~ 컴포넌트
public:
	UD1BombPlacementComponent* GetBombPlacement() const { return BombPlacementComp; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UD1BombPlacementComponent> BombPlacementComp;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UD1BomberCosmeticComponent> CosmeticComp;

//~ 이동·입력
public:
	/** 탑다운: 입력을 카메라 yaw 축으로 투영. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMove(float Right, float Forward);

protected:
	void OnMoveInput(const FInputActionValue& Value);

private:
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> PlaceBombAction;

//~ 속도 스탯
protected:
	/** PS OnSpeedLevelChanged 핸들러. SpeedLevel을 MaxWalkSpeed에 반영. */
	UFUNCTION()
	void OnSpeedLevelChanged();

private:
	/** 기본 이동속도(SpeedLevel 0). 생성자에서 MaxWalkSpeed에 적용. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BaseWalkSpeed = 500.f;

	/** Speed 아이템 1단계당 가산 속도. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float SpeedStep = 60.f;

//~ 폭발 피격·무적
public:
	/** 서버 전용: 폭발 피격 처리 진입점 */
	void ReceiveExplosionHit();

protected:
	UFUNCTION()
	void OnRep_Invulnerable();

private:
	/** 서버 전용: 비치명 피격 후 무적 시작. DurationSec 뒤 EndInvulnerability. */
	void StartInvulnerability(float DurationSec);
	/** 서버 전용. */
	void EndInvulnerability();

	/** 비치명 피격 후 무적 지속(초). 폭탄이 이 값으로 부여. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float HitInvulnSec = 2.f;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	bool bIsInvulnerable = false;

	FTimerHandle InvulnTimerHandle;

//~ 스턴
public:
	bool IsStunned() const { return bStunned; }

private:
	/** 서버 전용: 피격 경직 시작. StunDuration 동안 입력 차단. */
	void ApplyHitStun();

	/** 경직 해제. 타이머 콜백(서버). */
	void EndStun();

	/** 피격 경직(조작 불가) 지속 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float StunDurationSec = 1.0f;

	UPROPERTY(Replicated)
	bool bStunned = false;

	FTimerHandle StunTimerHandle;

//~ 사망 처리
public:
	/**
	 *  사망 정리. bIsAlive 복제로 서버·각 클라에서 1회씩 실행(bDeathHandled=인스턴스 재진입 가드).
	 *  서버(권위 전용): 콜리전/이동 차단·무적타이머 취소(죽은 폰 유일 teardown). 이동은 복제로 클라 수렴.
	 *  연출(몽타주/점멸/이름표)은 CosmeticComp가 담당(DS no-op).
	 */
	void HandleDeath();

protected:
	/** PS OnAliveStateChanged 핸들러. 클라까지 사망 정리 전파. */
	UFUNCTION()
	void OnPlayerAliveStateChanged();

private:
	bool bDeathHandled = false;

//~ 탈주 처리 (게임중 다른 기기 로그인 kick — 사망과 별개, 즉시 사라짐)
public:
	/** 탈주 정리. bLeft 복제로 서버·각 클라에서 실행. */
	void HandleLeft();

protected:
	/** PS OnLeftChanged 핸들러. */
	UFUNCTION()
	void OnPlayerLeftChanged();

private:
	bool bLeftHandled = false;

//~ PS 바인딩·이름표
protected:
	/** PS OnPlayerNameChanged 핸들러. 이름표 재푸시 위해 OnPlayerStateReady 재호출. */
	UFUNCTION()
	void OnPlayerNameRefreshed();

	/** PS 최초 확보 + 이름 갱신마다 호출. BP에서 이름표 등 UI 푸시용. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bomber|Events", meta = (DisplayName = "On Player State Ready"))
	void OnPlayerStateReady();

private:
	/** PossessedBy/OnRep_PlayerState 양쪽서 호출. PS 확보 시 OnAliveStateChanged 바인딩. */
	void RefreshPlayerStateBinding();

	/** 재바인딩 시 중복 방지·이전 핸들러 제거용. */
	TWeakObjectPtr<AD1BomberPlayerState> PSWeakPtr;
};
