// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "D1MapData.generated.h"

class AD1SoftBlock;
class AD1WallBlock;

/** 파싱된 스폰 지점 하나(셀 + 슬롯 0~3). */
struct FD1MapStart
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	int32 Slot = 0;
};

/** ASCII Rows를 파싱한 결과 — 런타임 스폰에 바로 쓰는 셀 목록. */
struct FD1MapLayout
{
	FIntPoint GridSize = FIntPoint::ZeroValue;
	TArray<FIntPoint> WallCells;
	TArray<FIntPoint> SoftBlockCells;
	TArray<FD1MapStart> Starts;
};

/**
 *  봄버맨 맵 한 장의 데이터. 하나의 쉘 umap에 주입해 런타임에 그리드를 스폰한다.
 *  레이아웃은 ASCII 행으로 작성(행=Y, 문자=X): '#'벽 'o'소프트블록 '.'빈칸 '1'~'4'스폰.
 *  셀 크기는 UD1BomberGridLibrary::CellSize(1m) 고정 — 가변은 그리드 "크기"(행·열 수).
 */
UCLASS(BlueprintType)
class UD1MapData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Rows를 파싱·검증해 OutLayout을 채운다. 실패 시 false + OutError. */
	bool BuildLayout(FD1MapLayout& OutLayout, FString& OutError) const;

	/** 결과 보고용 맵 이름 — MapName이 비어 있으면 애셋 오브젝트명으로 대체(백엔드 non-empty 검증 통과). */
	FString GetEffectiveMapName() const { return MapName.IsEmpty() ? GetName() : MapName; }

	TSubclassOf<AD1WallBlock> GetWallBlockClass() const { return WallBlockClass; }
	TSubclassOf<AD1SoftBlock> GetSoftBlockClass() const { return SoftBlockClass; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AD1WallBlock> WallBlockClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AD1SoftBlock> SoftBlockClass;

	/** 이 맵의 논리 이름(레벨/쉘 umap 이름과 무관). 매치 결과의 map_name으로 보고. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (AllowPrivateAccess = "true"))
	FString MapName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (AllowPrivateAccess = "true"))
	TArray<FString> Rows;
};
