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

//~ 공통
public:
	AD1BomberCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

//~ 폭탄 설치·추적
public:
	void NotifyBombDestroyed(AD1Bomb* Bomb);
	void AddIgnoredBomb(AD1Bomb* Bomb);

protected:
	UFUNCTION(Server, Reliable)
	void ServerTryPlaceBomb();

	/** 서버 전용: 죽은 weak ptr 정리 후 활성 폭탄 수 반환. */
	int32 GetActiveBombCount();

	UPROPERTY()
	TArray<TWeakObjectPtr<AD1Bomb>> ActiveBombs;

	UPROPERTY()
	TSet<TWeakObjectPtr<AD1Bomb>> IgnoredBombs;

private:
	/** 폭탄 설치 사전조건 전부(스턴·용량·페이즈·생존·격자·벽·중복셀). 서버 RPC 검증부. */
	bool CanPlaceBombAt(const FIntPoint& Cell, AD1BomberPlayerState* PS);

	void UpdateIgnoredBombs();

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1Bomb> BombClass;

//~ 폭발 피격·무적
public:
	/** 서버 전용: 폭발 피격 처리 진입점 */
	void ReceiveExplosionHit();

protected:
	UFUNCTION()
	void OnRep_Invulnerable();

private:
	/** 서버 전용: 비치명 피격 후 무적 시작. Duration 뒤 EndInvulnerability. */
	void StartInvulnerability(float Duration);
	void EndInvulnerability();

	/** 피격 시 재생 애니. DefaultSlot 동적 몽타주. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimSequenceBase> HitAnim;

	/** 비치명 피격 후 무적 지속(초). 폭탄이 이 값으로 부여. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float HitInvulnSec = 2.f;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, BlueprintReadOnly, Category = "Bomber", meta = (AllowPrivateAccess = "true"))
	bool bIsInvulnerable = false;

	FTimerHandle InvulnTimerHandle;

//~ 스턴
private:
	/** 서버 전용: 피격 경직 시작. StunDuration 동안 입력 차단. */
	void ApplyHitStun();

	/** 경직 해제. 타이머 콜백(서버). */
	void EndStun();

	/** 피격 경직(조작 불가) 지속 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float StunDuration = 1.0f;

	UPROPERTY(Replicated)
	bool bStunned = false;

	FTimerHandle StunTimerHandle;

//~ 점멸
private:
	/** 깜빡임 시작: 가시화 리셋 + 0.1s 토글 타이머 arm. 무적·사망 연출 공용. */
	void StartBlink();
	void TickBlink();

	FTimerHandle BlinkTimerHandle;
	bool bBlinkVisible = true;

//~ 사망 연출
public:
	/**
	 *  사망 정리. bIsAlive 복제로 서버·각 클라에서 1회씩 실행(bDeathHandled=인스턴스 재진입 가드).
	 *  서버(권위 전용): 콜리전/이동 차단·무적타이머 취소(죽은 폰 유일 teardown). 이동은 복제로 클라 수렴.
	 *  리슨호스트·클라: + 연출(몽타주/점멸/이름표).
	 */
	void HandleDeath();

protected:
	/** PS OnAliveStateChanged 핸들러. 클라까지 사망 정리 전파. */
	UFUNCTION()
	void OnPlayerAliveStateChanged();

private:
	/** 사망 연출 종료 후 메시 숨김. 타이머 콜백. */
	void FinishDeath();

	/** 사망 시 재생 몽타주. Auto Blend Out=off 권장. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** 사망 애니 종료 후 메시 숨김까지 추가 대기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float DeathHideDelay = 1.0f;

	FTimerHandle DeathHideTimerHandle;
	bool bDeathHandled = false;

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

//~ 공용 헬퍼
public:
	/** 박스 안의 봄버 캐릭터 수집(Pawn 오버랩 질의 공용화). */
	static void OverlapBomberCharacters(const UObject* WorldContext, const FVector& Center, const FVector& Extent, TArray<AD1BomberCharacter*>& OutChars);
};
