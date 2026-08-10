// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1UWSystemNotice.generated.h"

class UButton;
class UTextBlock;

/** 확인 버튼 클릭 통지. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSystemNoticeConfirmed);

/**
 *  범용 시스템 알림 모달(중앙 팝업) — 타이틀 + 메시지 + 확인 버튼.
 *  단일 세션 무효화 등에서 뷰포트 최상단에 띄운다. 색은 WBP_Ranking 팔레트 차용.
 */
UCLASS()
class UD1UWSystemNotice : public UD1UserWidget
{
	GENERATED_BODY()

protected:
	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	//~ End UUserWidget Interface

public:
	/** 타이틀·메시지 지정. AddToViewport 전/후 어느 순서로 불려도 반영(보류 후 NativeConstruct에서 적용). */
	void SetNotice(const FText& InTitle, const FText& InMessage);

	UPROPERTY(BlueprintAssignable, Category = "D1|Notice")
	FOnSystemNoticeConfirmed OnSystemNoticeConfirmed;

protected:
	UFUNCTION()
	void OnConfirmClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageLabel;

	/** 유일한 닫기 수단 — 없으면 모달이 입력을 영구 잠근다. 필수 바인딩으로 WBP 컴파일에서 강제. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ConfirmButton;

private:
	void ApplyPending();

	FText PendingTitle;
	FText PendingMessage;
	bool bHasPending = false;
};
