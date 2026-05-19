// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "D1Character.h"
#include "D1BomberCharacter.generated.h"

class UInputAction;
class AD1Bomb;

UCLASS(abstract)
class AD1BomberCharacter : public AD1Character
{
	GENERATED_BODY()

public:
	AD1BomberCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Top-down: project input onto camera-yaw axes. */
	virtual void DoMove(float Right, float Forward) override;

	/** Top-down: disable look input from controllers. */
	virtual void DoLook(float Yaw, float Pitch) override;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> PlaceBombAction;

	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TSubclassOf<AD1Bomb> BombClass;

	bool IsInvulnerable() const { return bIsInvulnerable; }

	/** Server-only. */
	void StartInvulnerability(float Duration);

	/** Server-only: clean up after own death (mesh hide, collision off). */
	void HandleDeath();

	/** Server-only: called by AD1Bomb when it detonates so the owner can place again. */
	void NotifyBombDestroyed(AD1Bomb* Bomb);

	/** Server-only: register a bomb that the character is currently overlapping.
	 *  As long as the character stays in the bomb's cell, the capsule treats
	 *  the bomb as non-blocking. Once the character leaves the cell, the Tick
	 *  cleanup re-enables blocking so the bomb can't be re-entered. */
	void AddIgnoredBomb(AD1Bomb* Bomb);

protected:
	UFUNCTION(Server, Reliable)
	void ServerTryPlaceBomb();

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable, BlueprintReadOnly, Category = "Bomber")
	bool bIsInvulnerable;

	UFUNCTION()
	void OnRep_Invulnerable();

	UPROPERTY()
	TWeakObjectPtr<AD1Bomb> ActiveBomb;

	UPROPERTY()
	TSet<TWeakObjectPtr<AD1Bomb>> IgnoredBombs;

private:
	FTimerHandle InvulnTimerHandle;
	FTimerHandle BlinkTimerHandle;
	bool bBlinkVisible;

	void EndInvulnerability();
	void TickBlink();
	void UpdateIgnoredBombs();
};
