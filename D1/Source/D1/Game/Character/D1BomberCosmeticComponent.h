// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "D1BomberCosmeticComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimSequenceBase;
class UMaterialInterface;

/** 순수 클라 연출 전담 컴포넌트(피격 애니·점멸·사망/탈주 연출·로컬 하이라이트). DS에선 전 기능 no-op. */
UCLASS()
class UD1BomberCosmeticComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 피격 리액션(몽타주+점멸). bWithAnim=false면 점멸만(사망 연출과 중첩 방지). */
	void PlayHitReaction(bool bWithAnim);

	/** 무적 종료 — 점멸 중단·가시성 복원. */
	void StopHitReaction();

	/** 사망 연출: 이름표 숨김 → 점멸 → 몽타주 → 종료 후 메시 숨김. */
	void PlayDeath();

	/** 탈주 연출 — 몽타주 없이 이름표·메시 즉시 숨김. */
	void PlayLeft();

	/** 로컬 플레이어 폰이면 오버레이 머티리얼 적용, 아니면 해제. 컨트롤러 확정마다 호출(멱등). */
	void RefreshLocalHighlight();

private:
	/** 깜빡임 시작: 가시화 리셋 + 토글 타이머 가동. 무적·사망 연출 공용. */
	void StartBlink();
	void TickBlink();

	/** 사망 연출 종료 후 메시 숨김. 타이머 콜백. */
	void FinishDeath();

	/** 캐릭터 머리 위 NameTag 등 위젯 컴포넌트 일괄 숨김. */
	void HideNameTags();

	/** 렌더 없는 데디 서버는 전 기능 생략. */
	bool ShouldRun() const;

	ACharacter* GetCharacterOwner() const;

	/** 피격 시 재생 애니. DefaultSlot 동적 몽타주. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimSequenceBase> HitAnim;

	/** 사망 시 재생 몽타주. Auto Blend Out=off 권장. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** 내 캐릭터 강조용 오버레이 머티리얼. BP에서 지정. */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	TObjectPtr<UMaterialInterface> LocalHighlightMaterial;

	/** 사망 애니 종료 후 메시 숨김까지 추가 대기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Bomber")
	float DeathHideDelay = 1.0f;

	FTimerHandle BlinkTimerHandle;
	FTimerHandle DeathHideTimerHandle;
	bool bBlinkVisible = true;
};
