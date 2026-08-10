// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/D1MatchTypes.h"
#include "Network/BackendTypes.h"

class AD1BomberGameState;

/**
 *  매치 결과 정산 조립 헬퍼 — 상태 없는 자유함수 (상속·Subsystem 불필요).
 *  UI 결과(FD1MatchResultEntry)와 백엔드 본문(FMatchResultPlayer)은 항상 쌍으로 조립한다 —
 *  두 배열의 인원이 어긋나면 백엔드가 매치 전체 결과를 거부한다.
 */
namespace D1MatchSettlement
{
	/** 결과(UI)와 백엔드 보고 본문에 한 명분을 동시 추가. 한 명 추가는 반드시 이 함수로. */
	void AppendResultPair(TArray<FD1MatchResultEntry>& InOutEntries, TArray<FMatchResultPlayer>& InOutPlayers,
		int64 UserId, int32 SlotIndex, int32 Placement, int32 LivesLeft, const FString& Nickname, bool bLeft);

	/**
	 * 최종 결과 두 배열을 한 번에 조립: PlayerArray 수집 + 탈주 기록 병합 + 미입장자 보정 +
	 * 결정적 정렬(등수 오름차순, 동률은 슬롯 순). SkipUserIds(kick·탈주 처리 유저)는 탈주 기록과의
	 * PlayerArray쪽 중복을 막는다. 등수 보정(공동 1위 등 판정)은 호출측 몫 — 여기는 읽기만 한다.
	 */
	void BuildFinalResults(const AD1BomberGameState& GS, const TSet<int64>& SkipUserIds,
		const TArray<FD1MatchResultEntry>& LeftEntries, const TArray<FMatchResultPlayer>& LeftPlayers,
		const TArray<FD1JoinEntry>& ExpectedRoster, int32 ExpectedPlayerCount,
		TArray<FD1MatchResultEntry>& OutEntries, TArray<FMatchResultPlayer>& OutPlayers);
}
