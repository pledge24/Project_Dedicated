// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1RankingSubsystem.generated.h"

class FJsonObject;

/**
 *  랭킹 조회 전담 Subsystem (클라).
 *  GET /api/ranking (Bearer). 응답을 FD1RankingResult로 파싱해 1회성 콜백으로 전달.
 */
UCLASS()
class UD1RankingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** GET /api/ranking?limit=&offset=. 완료 시 OnCompleted 실행(성공/실패 모두). */
	UFUNCTION(BlueprintCallable, Category = "Backend|Ranking")
	void FetchRanking(int32 Limit, int32 Offset, const FOnRankingCompleted& OnCompleted);

private:
	void HandleRankingResponse(const FHttpResponsePtr& Res, bool bSucceeded, FOnRankingCompleted Forward);
};
