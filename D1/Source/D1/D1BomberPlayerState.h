// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "D1BomberPlayerState.generated.h"

UCLASS()
class AD1BomberPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AD1BomberPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Lives, BlueprintReadOnly, Category = "Bomber")
	int32 Lives;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	bool bIsAlive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 Placement;

	/** 0~3. UI 카드 위치/테두리 색상 결정. GameMode가 ChoosePlayerStart에서 부여. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 PlayerSlotIndex;

	/** 서버 전용: 하트 1개 깎음. 사망 시 true 반환. */
	bool ApplyHit();

protected:
	UFUNCTION()
	void OnRep_Lives();
};
