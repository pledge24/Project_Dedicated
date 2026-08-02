// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1MatchSettlement.h"

#include "Core/D1LogChannels.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"

namespace
{
	/**
	 * 한 번도 입장하지 않은 roster 인원을 최하위 미입장자로 결과에 채운다.
	 * 이들은 PlayerState가 생긴 적이 없어 PlayerArray에도 탈주 캡처에도 없다 — 보정하지 않으면
	 * 백엔드 roster와 인원이 어긋나 정상 플레이한 나머지 인원의 결과까지 통째로 거부된다.
	 */
	void AppendNoShowResults(const TArray<FD1JoinEntry>& ExpectedRoster, int32 SlotCount,
		TArray<FD1MatchResultEntry>& InOutEntries, TArray<FMatchResultPlayer>& InOutPlayers)
	{
		// 이미 결과에 오른 신원과 좌석(봇이 쓴 좌석도 여기 포함되므로 그대로 피하면 된다).
		TSet<int64> ReportedUsers;
		TSet<int32> UsedSlots;
		for (const FMatchResultPlayer& RP : InOutPlayers)
		{
			ReportedUsers.Add(RP.UserId);
			UsedSlots.Add(RP.SlotIndex);
		}

		int32 NextFreeSlot = 0;
		for (const FD1JoinEntry& Expected : ExpectedRoster)
		{
			if (ReportedUsers.Contains(Expected.UserId))
			{
				continue;
			}

			// 좌석이 배정된 적이 없다(ChoosePlayerStart는 PostLogin에서 돈다) → 빈 자리를 하나 준다.
			// 백엔드가 slotIndex 유일성을 검증하고 DB에도 UNIQUE가 걸려 있다.
			while (UsedSlots.Contains(NextFreeSlot))
			{
				++NextFreeSlot;
			}
			UsedSlots.Add(NextFreeSlot);

			// Left=true로 보고하는 이유: 완주자 ELO 계산에서 빠져 정상 플레이한 사람들끼리만 점수가 오간다.
			// (Left=false면 미입장자가 실참가자로 ELO에 섞인다.) 입장 직후 나간 탈주자와 동일 취급이라
			// "안 들어오는 편이 이득"인 비대칭도 생기지 않는다.
			D1MatchSettlement::AppendResultPair(InOutEntries, InOutPlayers, Expected.UserId, NextFreeSlot,
				SlotCount, /*LivesLeft=*/0, Expected.Nickname, /*bLeft=*/true);

			UE_LOG(LogD1, Warning, TEXT("[Match] 미입장자 결과 보정 userId=%lld nickname=%s slot=%d placement=%d"),
				Expected.UserId, *Expected.Nickname, NextFreeSlot, SlotCount);
		}
	}
}

void D1MatchSettlement::AppendResultPair(TArray<FD1MatchResultEntry>& InOutEntries, TArray<FMatchResultPlayer>& InOutPlayers,
	int64 UserId, int32 SlotIndex, int32 Placement, int32 LivesLeft, const FString& Nickname, bool bLeft)
{
	FD1MatchResultEntry Entry;
	Entry.Placement = Placement;
	Entry.Nickname  = Nickname;
	Entry.SlotIndex = SlotIndex;
	Entry.LivesLeft = LivesLeft;
	InOutEntries.Add(Entry);

	FMatchResultPlayer Player;
	Player.UserId    = UserId;
	Player.SlotIndex = SlotIndex;
	Player.Placement = Placement;
	Player.LivesLeft = LivesLeft;
	Player.Left      = bLeft;
	InOutPlayers.Add(Player);
}

void D1MatchSettlement::BuildFinalResults(const AD1BomberGameState& GS, const TSet<int64>& SkipUserIds,
	const TArray<FD1MatchResultEntry>& LeftEntries, const TArray<FMatchResultPlayer>& LeftPlayers,
	const TArray<FD1JoinEntry>& ExpectedRoster, int32 ExpectedPlayerCount,
	TArray<FD1MatchResultEntry>& OutEntries, TArray<FMatchResultPlayer>& OutPlayers)
{
	OutEntries.Reserve(GS.PlayerArray.Num() + LeftEntries.Num());
	OutPlayers.Reserve(GS.PlayerArray.Num() + LeftPlayers.Num());
	for (const APlayerState* PS : GS.PlayerArray)
	{
		if (const AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			// 탈주 유저는 이미 LeftPlayers/Entries로 캡처됨 — PlayerArray쪽 중복 방지.
			// (Logout 지연으로 아직 PlayerArray에 남아있을 수 있다.)
			if (SkipUserIds.Contains(B->GetBackendUserId()))
			{
				continue;
			}

			AppendResultPair(OutEntries, OutPlayers, B->GetBackendUserId(), B->GetPlayerSlotIndex(),
				B->GetPlacement(), B->GetLives(), B->GetPlayerName(), /*bLeft=*/false);
		}
	}

	// 탈주자 병합 — 결과 인원이 roster와 정확히 일치해야 백엔드 검증 통과.
	OutEntries.Append(LeftEntries);
	OutPlayers.Append(LeftPlayers);

	// 한 번도 입장하지 않은 인원까지 채워야 그 "정확히 일치"가 성립한다.
	// PIE/standalone은 백엔드가 준 명단이 없어 보정할 기준 자체가 없다.
	if (ExpectedRoster.Num() > 0)
	{
		const int32 SlotCount = FMath::Max3(ExpectedPlayerCount, GS.PlayerArray.Num(), ExpectedRoster.Num());
		AppendNoShowResults(ExpectedRoster, SlotCount, OutEntries, OutPlayers);
	}

	// UI 표시용 결정적 순서: 등수 오름차순, 동률은 슬롯 순. (PlayerArray 순서는 비결정)
	OutEntries.Sort([](const FD1MatchResultEntry& A, const FD1MatchResultEntry& B)
	{
		return A.Placement != B.Placement ? A.Placement < B.Placement : A.SlotIndex < B.SlotIndex;
	});
}
