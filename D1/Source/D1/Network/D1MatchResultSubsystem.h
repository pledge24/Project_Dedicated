// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1MatchResultSubsystem.generated.h"

/**
 *  매치 생명주기 보고 전담 Subsystem (DS 서버권위) — 준비/결과/탈주.
 *  DS의 GameMode·MatchFlow만 C++로 호출. 매치별 서버 토큰을 Bearer로 첨부해 POST /api/match/*.
 *  클라 인증/매칭과 물리 분리 — 서버 권위 전송 경로를 격리한다.
 */
UCLASS()
class UD1MatchResultSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 서버 전용(DS): 맵 빌드·초기화 완료 시 호출. 백엔드가 이 콜백을 받고 클라에 match:found 전송. 전송 실패 시 소폭 재시도. */
	void ReportServerReady(const FString& MatchId, const FString& MatchToken);

	/** 서버 전용(DS): 매치 종료 시 호출. 매치별 서버 토큰을 Bearer로 첨부. */
	void ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
		int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players);

	/** 서버 전용(DS): 탈주 발생 즉시 호출. 백엔드가 최하위 확정값으로 점수를 바로 정산(로비 반영). */
	void ReportLeaver(const FString& MatchId, const FString& MatchToken, int64 UserId);

private:
	/** ReportServerReady 내부 구현. Attempt=재시도 회차(0부터). 전송 실패 시 백오프 후 자기 재호출. */
	void SendServerReady(const FString& MatchId, const FString& MatchToken, int32 Attempt);

	FTimerHandle ServerReadyRetryTimerHandle;
};
