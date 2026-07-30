// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/BackendTypes.h"
#include "D1MatchResultSubsystem.generated.h"

class FJsonObject;

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
	void ReportDSReady(const FString& MatchId, const FString& MatchToken);

	/**
	 * 서버 전용(DS): 매치 종료 시 호출. 매치별 서버 토큰을 Bearer로 첨부.
	 * 일시 실패(전송 실패·5xx·429)는 백오프 재시도 — 백엔드 재시작 창을 넘겨야 결과가 살아남는다.
	 * 전송이 확정되면(성공·409 멱등·확정 실패·재시도 소진) OnSettled를 정확히 한 번 실행한다.
	 * 호출측은 이 시점 이후에 DS를 종료해야 인플라이트 요청이 프로세스와 함께 사라지지 않는다.
	 */
	void ReportMatchResult(const FString& MatchId, const FString& MatchToken, const FString& MapName,
		int32 DurationSec, const FString& EndReason, const TArray<FMatchResultPlayer>& Players,
		const FSimpleDelegate& OnSettled);

	/** 서버 전용(DS): 탈주 발생 즉시 호출. 백엔드가 최하위 확정값으로 점수를 바로 정산(로비 반영). */
	void ReportLeaver(const FString& MatchId, const FString& MatchToken, int64 UserId);

private:
	/** ReportServerReady 내부 구현. Attempt=재시도 회차(0부터). 전송 실패 시 백오프 후 자기 재호출. */
	void SendServerReady(const FString& MatchId, const FString& MatchToken, int32 Attempt);

	/** ReportMatchResult 내부 구현. Body는 회차 간 재사용해 재전송 페이로드 동일성을 보장한다. */
	void SendMatchResult(const FString& MatchId, const FString& MatchToken, const TSharedRef<FJsonObject>& Body,
		int32 Attempt, const FSimpleDelegate& OnSettled);

	FTimerHandle ServerReadyRetryTimerHandle;
	FTimerHandle MatchResultRetryTimerHandle;
};
