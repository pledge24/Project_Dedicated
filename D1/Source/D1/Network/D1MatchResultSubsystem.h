// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1MatchResultSubsystem.generated.h"

/**
 *  매치 결과 보고 전담 Subsystem (DS 서버권위).
 *  DS의 GameMode만 C++로 호출. 매치별 서버 토큰을 Bearer로 첨부해 POST /api/match/result.
 *  클라 인증/매칭과 물리 분리 — 결과 전송 경로를 서버 권위로 격리한다.
 */
UCLASS()
class UD1MatchResultSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 서버 전용(DS): 매치 종료 시 호출. 매치별 서버 토큰을 Bearer로 첨부. */
	void ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
		int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players);

	/** 서버 전용(DS): 탈주 발생 즉시 호출. 백엔드가 최하위 확정값으로 점수를 바로 정산(로비 반영). */
	void ReportLeaver(const FString& MatchId, const FString& MatchToken, int64 UserId);
};
