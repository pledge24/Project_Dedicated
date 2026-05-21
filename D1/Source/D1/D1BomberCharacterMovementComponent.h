// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "D1BomberCharacterMovementComponent.generated.h"

/**
 *  봄버맨 캐릭터 전용 CMC.
 *  현재는 부모와 동일 동작. 향후 폭탄/그리드 관련 이동 보정이 필요해질 때 확장점으로 유지.
 *  (BP_BomberCharacter의 CharMoveComp가 이 클래스를 참조하고 있어 함부로 삭제 금지 —
 *   삭제하면 BP 로드 시 컴포넌트 None되어 AnimBP가 크래시.)
 */
UCLASS()
class UD1BomberCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
};
