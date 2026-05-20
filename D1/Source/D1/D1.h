// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

/** 프로젝트 전역 로그 카테고리 */
DECLARE_LOG_CATEGORY_EXTERN(LogD1, Log, All);

/** 폭탄 전용 충돌 채널.
 *  DefaultEngine.ini의 [/Script/Engine.CollisionProfile]에 Name="Bomb"로 설정됨. */
#define ECC_Bomb ECC_GameTraceChannel1