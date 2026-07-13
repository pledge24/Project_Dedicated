// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1UWQuitButton.generated.h"

class UButton;

/**
 *  게임 나가기 버튼 위젯. 메뉴/로비 우하단 상주, 클릭 시 앱 즉시 종료.
 *  AD1MenuPlayerController가 뷰포트 상주 오버레이로 띄운다.
 */
UCLASS()
class UD1UWQuitButton : public UD1UserWidget
{
	GENERATED_BODY()

protected:
	//~ Begin UUserWidget Interface
	virtual void NativeConstruct() override;
	//~ End UUserWidget Interface

	UFUNCTION()
	void OnQuitClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> QuitButton;
};
