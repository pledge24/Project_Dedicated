// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1UWAuthBase.generated.h"

class UBackendSubsystem;
class UButton;
class UEditableTextBox;
class UTextBlock;
struct FBackendResponse;

/**
 *  로그인/회원가입 공용 베이스.
 *  요청 잠금/해제, 에러 텍스트 표시를 한 곳에서.
 *  자식 위젯은 NativeConstruct에서 버튼 OnClicked 바인딩만 하면 된다.
 */
UCLASS(abstract)
class UD1UWAuthBase : public UD1UserWidget
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

	//~ 공용 인증 플로우 (자식 클릭/완료 핸들러가 사용)
	UBackendSubsystem* ResolveBackend();
	void BeginAuthSubmit();
	bool FinishAuthSubmit(const FBackendResponse& Response);

	/** 자식이 잠금/해제 대상 submit 버튼을 반환. */
	virtual UButton* GetSubmitButton() const { return nullptr; }

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UEditableTextBox> LoginIdTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordTextBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ErrorLabel;
};
