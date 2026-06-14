// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1SoftBlock.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMeshComponent;
enum class EPowerupType : uint8;

UCLASS()
class AD1SoftBlock : public AActor
{
	GENERATED_BODY()

public:
	AD1SoftBlock();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 전용: 폭발에 맞아 "파괴 중" 진입. 일정 시간 반투명 유지 후 Destroy.
	 *  파괴 중에도 콜리전·폭발 차단은 그대로(셀은 CompleteDestruction에서 제거). */
	void StartDying();

	/** 서버 전용: 빌드 시 이 블록이 숨길 파워업을 사전 배정. 파괴 완료 시 그대로 스폰. */
	void SetHeldItem(EPowerupType InType);

	bool IsDying() const { return bDying; }

protected:
	UFUNCTION()
	void OnRep_bDying();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

private:
	/** 파괴 중 시간 경과 후(서버): 자기 셀 제거 + 액터 Destroy. */
	void CompleteDestruction();

	/** 파괴 중 상태에서 교체할 반투명 머티리얼. 없으면 반투명 표현만 생략. */
	UPROPERTY(EditAnywhere, Category = "Bomber")
	TObjectPtr<UMaterialInterface> DyingMaterial;

	/** "파괴 중" 유지 시간(초). 이 시간 뒤 실제 Destroy. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float DyingDurationSec = 0.6f;

	UPROPERTY(ReplicatedUsing = OnRep_bDying)
	bool bDying = false;

	FTimerHandle DyingTimerHandle;

	/** 빌드 시 사전 배정된 보유 아이템(서버 전용, 비복제 — 파괴 전엔 숨김). */
	EPowerupType HeldItem{};

	/** 보유 아이템 유무. true일 때만 파괴 시 스폰. */
	bool bHasItem = false;
};
