// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1SoftBlock.generated.h"

class AD1PowerupPickup;
class UBoxComponent;
class UMaterialInterface;
class UStaticMeshComponent;
enum class EPowerupType : uint8;

/** 파괴 가능 블록 — 파괴 연출 후 보유 아이템 드롭. */
UCLASS()
class AD1SoftBlock : public AActor
{
	GENERATED_BODY()

public:
	AD1SoftBlock();

	//~ Begin AActor Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor Interface

//~ 컴포넌트
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

//~ 파괴 연출
public:
	/** 서버 전용: "파괴 중" 진입. 반투명 유지 후 Destroy. 콜리전·폭발 차단은 유지. */
	void StartDestroying();

	bool IsDestroying() const { return bDestroying; }

protected:
	UFUNCTION()
	void OnRep_bDestroying();

private:
	/** 파괴 중 시간 경과 후(서버): 자기 셀 제거 + 액터 Destroy. */
	void CompleteDestruction();

	/** 파괴 중 상태에서 교체할 반투명 머티리얼. 없으면 반투명 표현만 생략. */
	UPROPERTY(EditAnywhere, Category = "Bomber")
	TObjectPtr<UMaterialInterface> DestroyingMaterial;

	/** "파괴 중" 유지 시간(초). 이 시간 뒤 실제 Destroy. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float DestroyingDurationSec = 0.6f;

	UPROPERTY(ReplicatedUsing = OnRep_bDestroying)
	bool bDestroying = false;

	FTimerHandle DestroyingTimerHandle;

//~ 아이템 드롭
public:
	/** 서버 전용: 빌드 시 숨길 파워업·드롭 방법 사전 배정. 파괴 시 스폰. */
	void SetHeldItem(EPowerupType InType, TSubclassOf<AD1PowerupPickup> InPickupClass, float InDropZ);

private:
	/** 빌드 시 사전 배정된 보유 아이템(서버 전용, 비복제 — 파괴 전엔 숨김). */
	EPowerupType HeldItem{};

	/** 드롭할 픽업 클래스·높이(서버 전용, 빌드 시 GameMode가 주입). */
	UPROPERTY()
	TSubclassOf<AD1PowerupPickup> PickupClass;

	float DropZ = 40.f;

	/** true일 때만 파괴 시 스폰. */
	bool bHasItem = false;
};
