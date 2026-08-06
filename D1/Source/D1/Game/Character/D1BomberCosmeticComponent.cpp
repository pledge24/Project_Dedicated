// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/Character/D1BomberCosmeticComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

void UD1BomberCosmeticComponent::PlayHitReaction(bool bWithAnim)
{
	if (!ShouldRun())
	{
		return;
	}

	ACharacter* OwnerChar = GetCharacterOwner();
	if (!OwnerChar)
	{
		return;
	}

	// 피격 리액션: DefaultSlot에 동적 몽타주로 재생. invuln 복제로 렌더 인스턴스(클라·리슨호스트) 전부 재생.
	if (bWithAnim && HitAnim)
	{
		if (UAnimInstance* AnimInst = OwnerChar->GetMesh() ? OwnerChar->GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInst->PlaySlotAnimationAsDynamicMontage(HitAnim, TEXT("DefaultSlot"));
		}
	}

	StartBlink();
}

void UD1BomberCosmeticComponent::StopHitReaction()
{
	if (!ShouldRun())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(BlinkTimerHandle);
	ACharacter* OwnerChar = GetCharacterOwner();
	if (USkeletalMeshComponent* SK = OwnerChar ? OwnerChar->GetMesh() : nullptr)
	{
		SK->SetVisibility(true);
	}
}

void UD1BomberCosmeticComponent::PlayDeath()
{
	if (!ShouldRun())
	{
		return;
	}

	ACharacter* OwnerChar = GetCharacterOwner();
	if (!OwnerChar)
	{
		return;
	}

	HideNameTags();

	// 사망 블링크: 이전 blink 위상과 무관하게 메시를 보이는 상태로 맞춘 뒤 시작.
	if (USkeletalMeshComponent* SK = OwnerChar->GetMesh())
	{
		SK->SetVisibility(true);
	}
	StartBlink();

	// 사망 몽타주 재생 후, 재생 길이 + 여유 시간 뒤 메시 숨김.
	float HideAfter = DeathHideDelay;
	if (DeathMontage)
	{
		if (UAnimInstance* AnimInst = OwnerChar->GetMesh() ? OwnerChar->GetMesh()->GetAnimInstance() : nullptr)
		{
			const float MontageLen = AnimInst->Montage_Play(DeathMontage);
			if (MontageLen > 0.f)
			{
				HideAfter = MontageLen + DeathHideDelay;
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(DeathHideTimerHandle, this,
		&UD1BomberCosmeticComponent::FinishDeath, HideAfter, false);
}

void UD1BomberCosmeticComponent::PlayLeft()
{
	if (!ShouldRun())
	{
		return;
	}

	// 탈주는 몽타주/블링크 없이 즉시 사라짐 — 이름표 + 메시 숨김.
	HideNameTags();
	ACharacter* OwnerChar = GetCharacterOwner();
	if (USkeletalMeshComponent* SK = OwnerChar ? OwnerChar->GetMesh() : nullptr)
	{
		SK->SetVisibility(false);
	}
}

void UD1BomberCosmeticComponent::RefreshLocalHighlight()
{
	if (!ShouldRun())
	{
		return;
	}

	ACharacter* OwnerChar = GetCharacterOwner();
	if (!OwnerChar)
	{
		return;
	}

	// '내 캐릭터' = 로컬 PlayerController가 빙의한 폰. 봇(AIController)·원격 폰은 로컬 PC가 아니라 제외.
	const APlayerController* PC = Cast<APlayerController>(OwnerChar->GetController());
	const bool bIsLocalPlayerPawn = PC && PC->IsLocalController();

	UMaterialInterface* Overlay = bIsLocalPlayerPawn ? LocalHighlightMaterial.Get() : nullptr;
	if (USkeletalMeshComponent* SK = OwnerChar->GetMesh())
	{
		SK->SetOverlayMaterial(Overlay);
	}
}

void UD1BomberCosmeticComponent::StartBlink()
{
	bBlinkVisible = true;
	GetWorld()->GetTimerManager().SetTimer(BlinkTimerHandle, this,
		&UD1BomberCosmeticComponent::TickBlink, 0.1f, true);
}

void UD1BomberCosmeticComponent::TickBlink()
{
	bBlinkVisible = !bBlinkVisible;
	ACharacter* OwnerChar = GetCharacterOwner();
	if (USkeletalMeshComponent* SK = OwnerChar ? OwnerChar->GetMesh() : nullptr)
	{
		SK->SetVisibility(bBlinkVisible);
	}
}

void UD1BomberCosmeticComponent::FinishDeath()
{
	GetWorld()->GetTimerManager().ClearTimer(BlinkTimerHandle);
	ACharacter* OwnerChar = GetCharacterOwner();
	if (USkeletalMeshComponent* SK = OwnerChar ? OwnerChar->GetMesh() : nullptr)
	{
		SK->SetVisibility(false);
	}
}

void UD1BomberCosmeticComponent::HideNameTags()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	TArray<UWidgetComponent*> WidgetComps;
	OwnerActor->GetComponents<UWidgetComponent>(WidgetComps);
	for (UWidgetComponent* WC : WidgetComps)
	{
		WC->SetVisibility(false);
	}
}

bool UD1BomberCosmeticComponent::ShouldRun() const
{
	return GetNetMode() != NM_DedicatedServer;
}

ACharacter* UD1BomberCosmeticComponent::GetCharacterOwner() const
{
	return Cast<ACharacter>(GetOwner());
}
