// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "D1SoftBlock.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMeshComponent;

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
};
