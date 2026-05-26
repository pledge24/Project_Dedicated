// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "D1BomberPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLivesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAliveStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerNameChanged);

UCLASS()
class AD1BomberPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AD1BomberPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버 전용: 하트 1개 깎음. 사망 시 true 반환. */
	bool ApplyHit();

	/** Lives 값이 클라에 복제됐을 때 브로드캐스트. UI 바인딩용. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnLivesChanged OnLivesChanged;

	/** bIsAlive 값이 클라에 복제됐을 때 브로드캐스트. UI 바인딩용. */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnAliveStateChanged OnAliveStateChanged;

	/** PlayerName이 바뀌었을 때 브로드캐스트. 이름표 UI 갱신용.
	 *  엔진 SetPlayerName이 Listen Server에서도 OnRep_PlayerName을 수동 호출하므로
	 *  서버 자기 자신 PS도 트리거됨 (Dedicated Server는 UI 없으니 무관). */
	UPROPERTY(BlueprintAssignable, Category = "Bomber|Events")
	FOnPlayerNameChanged OnPlayerNameChanged;

	UPROPERTY(ReplicatedUsing = OnRep_Lives, BlueprintReadOnly, Category = "Bomber")
	int32 Lives;

	UPROPERTY(ReplicatedUsing = OnRep_bIsAlive, BlueprintReadOnly, Category = "Bomber")
	bool bIsAlive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 Placement;

	/** 0~3. UI 카드 위치/테두리 색상 결정. GameMode가 ChoosePlayerStart에서 부여. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Bomber")
	int32 PlayerSlotIndex;

protected:
	virtual void OnRep_PlayerName() override;

	UFUNCTION()
	void OnRep_Lives();

	UFUNCTION()
	void OnRep_bIsAlive();
};
