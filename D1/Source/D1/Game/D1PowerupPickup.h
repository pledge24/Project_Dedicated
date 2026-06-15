// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1PowerupPickup.generated.h"

class UMaterialBillboardComponent;
class UMaterialInterface;
class USphereComponent;

UENUM(BlueprintType)
enum class EPowerupType : uint8
{
	Fire   UMETA(DisplayName = "Fire"),
	Bomb   UMETA(DisplayName = "Bomb"),
	Speed  UMETA(DisplayName = "Speed")
};

/**
 *  소프트블록 파괴 자리에 드롭되는 파워업. 서버가 스폰·타입 지정, 캐릭터 오버랩 시
 *  PlayerState 스탯을 올리고 사라진다. 비주얼은 카메라를 향하는 빌보드 + 위아래 둥둥(bob).
 */
UCLASS()
class AD1PowerupPickup : public AActor
{
	GENERATED_BODY()

public:
	AD1PowerupPickup();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 전용: 스폰 직후 타입 지정(드롭 시 GameMode가 호출). */
	void SetPowerupType(EPowerupType InType);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_PowerupType();

	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMaterialBillboardComponent> BillboardComp;

private:
	/** PowerupType에 맞는 아이콘 머티리얼로 빌보드 갱신. */
	void RefreshVisual();

	/** 타입별 아이콘 머티리얼 — EPowerupType 인덱스(Fire=0, Bomb=1, Speed=2) 순. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TArray<TObjectPtr<UMaterialInterface>> IconMaterials;

	/** 빌보드 한 변 크기(월드 cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BillboardSize = 60.f;

	/** 둥둥 진폭(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BobAmplitude = 15.f;

	/** 둥둥 속도(rad/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BobSpeed = 3.f;

	/** 빌보드 기준 높이(cm). bob은 이 값 기준으로 위아래. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float BillboardBaseZ = 50.f;

	UPROPERTY(ReplicatedUsing = OnRep_PowerupType, meta = (AllowPrivateAccess = "true"))
	EPowerupType PowerupType = EPowerupType::Fire;

	/** 액터별 bob 위상 오프셋(동시 흔들림 방지). 스폰 위치로 결정. */
	float BobPhase = 0.f;
};
