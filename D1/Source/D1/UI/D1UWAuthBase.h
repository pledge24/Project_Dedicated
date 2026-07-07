// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/D1UserWidget.h"
#include "D1UWAuthBase.generated.h"

class UD1AuthSubsystem;
class UButton;
class UEditableTextBox;
class UTextBlock;
struct FBackendResponse;

/**
 *  로그인/회원가입 공용 베이스 클래스(상속 전용).
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

	/** 요청 시작전 처리 — 에러 텍스트 숨김. 자식이 자기 버튼 비활성화. */
	UFUNCTION(BlueprintCallable, Category = "Auth")
	void BeginRequest();

	/** 요청 종료(Res 수신)전 처리 — 실패 시 에러 텍스트 표시. */
	UFUNCTION(BlueprintCallable, Category = "Auth")
	void EndRequest(bool bSuccess, const FString& ErrorMessage);

	/** 요청 시작 이벤트 바인딩 함수 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Auth|Events")
	void OnRequestStarted();

	/** 요청 종료(Res 수신) 이벤트 바인딩 함수 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Auth|Events")
	void OnRequestFinished(bool bSuccess, const FString& ErrorMessage);

	//~ 공용 인증 플로우 (자식 클릭/완료 핸들러가 사용)
	UD1AuthSubsystem* ResolveBackend();
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
