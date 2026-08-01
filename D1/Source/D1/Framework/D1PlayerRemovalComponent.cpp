// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1PlayerRemovalComponent.h"

#include "Core/D1LogChannels.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchFlowComponent.h"
#include "Framework/D1MatchSettlement.h"
#include "Framework/D1PlayerController.h"
#include "Network/D1MatchResultSubsystem.h"
#include "Network/D1OnlineSettings.h"
#include "TimerManager.h"

UD1PlayerRemovalComponent::UD1PlayerRemovalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UD1PlayerRemovalComponent::InitializeRemoval(int32 InExpectedPlayers, const FString& InMatchId, const FString& InMatchToken)
{
	if (!HasServerAuthority())
	{
		return;
	}

	ExpectedPlayerCount = InExpectedPlayers;
	CurrentMatchId      = InMatchId;
	CurrentMatchToken   = InMatchToken;

	// 게임중 강제 회수(다른 기기 로그인) 폴링 시작 — 토큰 있는 실 DS에서만.
	StartKickPolling();
}

void UD1PlayerRemovalComponent::NotifyPlayerDisconnected(AController* Exiting)
{
	if (!HasServerAuthority() || !Exiting)
	{
		return;
	}

	AD1BomberPlayerState* PS = Exiting->GetPlayerState<AD1BomberPlayerState>();
	if (!PS)
	{
		return;
	}

	const int64 UserId = PS->GetBackendUserId();

	// 봇·이미 탈주·이미 kick 처리·매치 미시작/종료는 제외 — 진행 중 실유저 이탈만 탈주로.
	if (PS->IsBot() || PS->HasLeft() || UserId <= 0 || KickedUserIds.Contains(UserId)
		|| !HasMatchStarted() || IsMatchEnded())
	{
		return;
	}

	KickedUserIds.Add(UserId); // 재입장 거절 + 중복 방지
	ProcessLeaver(PS, /*bNotifyClient=*/false);
}

void UD1PlayerRemovalComponent::StartKickPolling()
{
	// 백엔드가 띄운 DS(토큰 보유)에서만 — PIE/standalone은 폴링 없음.
	if (CurrentMatchToken.IsEmpty())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(1.f, GetDefault<UD1OnlineSettings>()->KickPollIntervalSec);
		World->GetTimerManager().SetTimer(
			KickPollTimerHandle, this, &UD1PlayerRemovalComponent::PollKicks, Interval, /*bLoop=*/true);
	}
}

void UD1PlayerRemovalComponent::StopKickPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(KickPollTimerHandle);
	}
}

void UD1PlayerRemovalComponent::PollKicks()
{
	UD1MatchResultSubsystem* Result = GetResultClient();
	if (CurrentMatchId.IsEmpty() || !Result)
	{
		return;
	}

	TWeakObjectPtr<UD1PlayerRemovalComponent> WeakThis(this);
	Result->FetchKicks(CurrentMatchId, CurrentMatchToken,
		[WeakThis](const TArray<int64>& UserIds)
		{
			UD1PlayerRemovalComponent* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			for (const int64 UserId : UserIds)
			{
				Self->HandleKickUser(UserId);
			}
		});
}

void UD1PlayerRemovalComponent::HandleKickUser(int64 UserId)
{
	if (!HasServerAuthority() || UserId <= 0 || KickedUserIds.Contains(UserId))
	{
		return;
	}

	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS)
	{
		return;
	}

	AD1BomberPlayerState* Target = nullptr;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			if (B->GetBackendUserId() == UserId)
			{
				Target = B;
				break;
			}
		}
	}

	// 표시(중복·재입장 방어). 미접속 유저면 접속 시 PreLogin이 거절.
	KickedUserIds.Add(UserId);

	if (!Target)
	{
		return;
	}

	// 매치가 이미 끝났으면 결과는 확정 — 통지만(로그인 복귀).
	if (IsMatchEnded())
	{
		if (AD1PlayerController* PC = Cast<AD1PlayerController>(Target->GetOwningController()))
		{
			PC->ClientNotifySessionSuperseded();
		}
		return;
	}

	ProcessLeaver(Target, /*bNotifyClient=*/true);
}

void UD1PlayerRemovalComponent::ProcessLeaver(AD1BomberPlayerState* Target, bool bNotifyClient)
{
	AD1BomberGameState* GS = GetBomberGameState();
	if (!GS || !Target)
	{
		return;
	}

	const int64 UserId = Target->GetBackendUserId();

	// 탈주 처리(사망과 별개). 전원 꼴등(정원 고정), 캐릭터 사라짐, 결과 캡처(Logout로 빠지기 전).
	const int32 LastPlacement = FMath::Max(ExpectedPlayerCount, GS->PlayerArray.Num());
	Target->SetPlacement(LastPlacement);
	Target->SetLeft(); // bLeft 복제 → 캐릭터 사라짐 + 카드 "탈주"

	// PS가 제거돼도(끊김/kick 후 disconnect) 카드가 "탈주"를 매치 끝까지 유지하도록 슬롯을 GameState에 복제 기록.
	GS->MarkSlotLeft(Target->GetPlayerSlotIndex(), Target->GetPlayerName());

	D1MatchSettlement::AppendResultPair(LeftEntries, LeftPlayers, UserId, Target->GetPlayerSlotIndex(),
		LastPlacement, Target->GetLives(), Target->GetPlayerName(), /*bLeft=*/true);

	// 탈주 즉시 정산 — 백엔드가 최하위 확정값을 바로 반영(로비 즉시 반영). 토큰 있는 실 DS만.
	if (UD1MatchResultSubsystem* ResultClient = GetResultClient())
	{
		ResultClient->ReportLeaver(CurrentMatchId, CurrentMatchToken, UserId);
	}

	// 클라 통지(팝업 + 로그인 복귀) + 남은 시간 입력 차단. 끊김(disconnect)은 이미 떠나 생략.
	if (bNotifyClient)
	{
		if (AD1PlayerController* PC = Cast<AD1PlayerController>(Target->GetOwningController()))
		{
			PC->ClientNotifySessionSuperseded();
			PC->DisableInput(PC);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 탈주 처리 userId=%lld placement=%d"), UserId, LastPlacement);

	// 종료 판정은 심판(MatchFlow) 소유 — 생존 목록 제외·다음 틱 평가 예약을 위임.
	// 한 배치(kick 폴링 응답)·한 프레임의 탈주를 심판이 모아 한 번만 판정 → 앞 탈주가 매치를 끝내
	// 뒤 탈주가 스킵되던 순서 의존 제거. 전원 탈주는 생존 0 → Draw로 정확 판정.
	if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
	{
		Flow->NotifyPlayerLeft(Target);
	}
}

bool UD1PlayerRemovalComponent::HasMatchStarted() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->GetMatchPhase() != EBomberMatchPhase::Waiting;
}

bool UD1PlayerRemovalComponent::IsMatchEnded() const
{
	const AD1BomberGameState* GS = GetBomberGameState();
	return GS && GS->GetMatchPhase() == EBomberMatchPhase::Finished;
}

AD1BomberGameState* UD1PlayerRemovalComponent::GetBomberGameState() const
{
	return Cast<AD1BomberGameState>(GetOwner());
}

UD1MatchResultSubsystem* UD1PlayerRemovalComponent::GetResultClient() const
{
	if (CurrentMatchToken.IsEmpty())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UD1MatchResultSubsystem>() : nullptr;
}

bool UD1PlayerRemovalComponent::HasServerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
