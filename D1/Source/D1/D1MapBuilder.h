// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "D1MapBuilder.generated.h"

class AD1BomberGameState;
class AD1PowerupPickup;
class UD1MapData;

/** 맵 빌드 설정. 서버 BeginPlay에서 GameMode UPROPERTY로 채워 전달하는 트랜지언트 값. */
struct FD1MapBuildConfig
{
	/** BP 기본 맵 데이터. `-MapData=` 커맨드라인이 있으면 그쪽이 우선. */
	const UD1MapData* DefaultMap = nullptr;

	/** 블록 스폰 Z(셀 중심). */
	float BlockZ = 50.f;

	/** 드롭 픽업 클래스. 미지정이면 사전 배정 안 함. */
	TSubclassOf<AD1PowerupPickup> PickupClass;

	/** 소프트블록 1개당 드롭 확률(0~1). */
	float DropChance = 0.3f;

	/** Fire/Bomb/Speed 드롭 가중치. */
	int32 FireWeight = 1;
	int32 BombWeight = 1;
	int32 SpeedWeight = 1;

	/** 파워업 스폰 Z(셀 중심). */
	float PowerupZ = 40.f;
};

/**
 *  MapData(ASCII)를 런타임 월드로 빌드하는 정적 헬퍼(서버 전용).
 *  그리드/벽/소프트블록/스폰을 스폰하고 GameState 권위 데이터를 채운다.
 */
UCLASS()
class UD1MapBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 서버 전용: Cfg로 맵을 빌드(GS 채움 + 액터 스폰). 실패 시 false + OutError. */
	static bool Build(UWorld* World, AD1BomberGameState* GS, const FD1MapBuildConfig& Cfg, FString& OutError);
};
