// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "D1AuthWidgetBase.generated.h"

class UTextBlock;

/**
 *  로그인/회원가입 공용 베이스.
 *  요청 잠금/해제, 에러 텍스트 표시를 한 곳에서.
 *  자식 위젯은 NativeConstruct에서 버튼 OnClicked 바인딩만 하면 된다.
 */
UCLASS(abstract)
class UD1AuthWidgetBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	//~ UUserWidget
	virtual void NativeConstruct() override;

	/** 통신 시작 — 에러 텍스트 숨김. 자식이 자기 버튼 비활성화. */
	UFUNCTION(BlueprintCallable, Category = "Auth")
	void BeginRequest();

	/** 응답 도착 — 실패 시 에러 텍스트 표시. */
	UFUNCTION(BlueprintCallable, Category = "Auth")
	void EndRequest(bool bSuccess, const FString& ErrorMessage);

	UFUNCTION(BlueprintImplementableEvent, Category = "Auth|Events")
	void OnRequestStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Auth|Events")
	void OnRequestFinished(bool bSuccess, const FString& ErrorMessage);

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TextBlock_Error;
};
