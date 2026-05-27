// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "D1UserWidget.generated.h"

/**
 *  프로젝트 공용 위젯 베이스.
 *  모든 D1 위젯(C++/BP)은 UUserWidget 대신 이 클래스를 상속한다.
 */
UCLASS(abstract)
class UD1UserWidget : public UUserWidget
{
	GENERATED_BODY()
};
