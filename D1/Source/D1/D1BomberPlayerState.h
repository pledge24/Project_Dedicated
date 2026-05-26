// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "D1BomberPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLivesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAliveStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerNameChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlotIndexChanged);

UCLASS()
class AD1BomberPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AD1BomberPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**-------------------
	 *	  API Function
	 *	  (Server-Only)
	 *-------------------*/
	
	bool ApplyHit();
	void SetPlayerSlotIndex(int32 NewIndex);
	
	/**-------------------
	 *	    API Data
	 *-------------------*/
	
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnLivesChanged OnLivesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnAliveStateChanged OnAliveStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerNameChanged OnPlayerNameChanged;

	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnSlotIndexChanged OnSlotIndexChanged;

	UPROPERTY(ReplicatedUsing = OnRep_Lives, BlueprintReadOnly, Category = "Bomber")
	int32 Lives;

	UPROPERTY(ReplicatedUsing = OnRep_bIsAlive, BlueprintReadOnly, Category = "Bomber")
	bool bIsAlive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 Placement;

	UPROPERTY(ReplicatedUsing = OnRep_PlayerSlotIndex, BlueprintReadOnly, Category = "Bomber")
	int32 PlayerSlotIndex;

protected:
	virtual void OnRep_PlayerName() override;

	UFUNCTION()
	void OnRep_Lives();

	UFUNCTION()
	void OnRep_bIsAlive();

	UFUNCTION()
	void OnRep_PlayerSlotIndex();
};
