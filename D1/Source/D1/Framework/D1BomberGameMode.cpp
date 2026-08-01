// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1BotController.h"
#include "Framework/D1MatchFlowComponent.h"
#include "Systems/Map/D1MapBuilder.h"
#include "Systems/Map/D1MapData.h"
#include "Framework/D1MatchTypes.h"
#include "Core/D1LogChannels.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// 이름순 정렬 — 슬롯 인덱스 일관성 확보.
	void GetSortedPlayerStarts(const UObject* WorldContext, TArray<AActor*>& OutStarts)
	{
		UGameplayStatics::GetAllActorsOfClass(WorldContext, APlayerStart::StaticClass(), OutStarts);
		OutStarts.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetName() < B.GetName();
		});
	}

	int32 ResolveSlotFromTag(const AActor* Start, int32 DefaultSlot)
	{
		if (const APlayerStart* PlayerStart = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PlayerStart->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 TagNum = FCString::Atoi(*TagStr);
				if (0 <= TagNum && TagNum < D1MaxPlayerSlots)
				{
					return TagNum;
				}
			}
		}
		return DefaultSlot;
	}
}

AD1BomberGameMode::AD1BomberGameMode()
{
	GameStateClass = AD1BomberGameState::StaticClass();
	PlayerStateClass = AD1BomberPlayerState::StaticClass();
	BotControllerClass = AD1BotController::StaticClass();
}

void AD1BomberGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// 재입장 거절 — ?join= 토큰이 roster에 있고 그 유저가 이미 kick(다른 기기 로그인)됐으면 연결 거부.
	const FString JoinToken = UGameplayStatics::ParseOption(Options, TEXT("join"));
	if (JoinToken.IsEmpty())
	{
		return;
	}

	const FD1JoinEntry* Entry = MatchConfig.Roster.Find(JoinToken);
	if (!Entry)
	{
		return;
	}

	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (const UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
		{
			if (Flow->IsUserKicked(Entry->UserId))
			{
				ErrorMessage = TEXT("세션이 다른 기기 로그인으로 종료되어 재입장할 수 없습니다.");
				UE_LOG(LogD1, Warning, TEXT("[Match] PreLogin 거절 — kick된 유저 재입장 시도 userId=%lld"), Entry->UserId);
			}
		}
	}
}

void AD1BomberGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
		{
			Flow->HandlePlayerJoined();
		}
	}
}

void AD1BomberGameMode::Logout(AController* Exiting)
{
	// 매치 진행 중 이탈(접속 끊김/나가기)은 탈주로 처리 — Super가 PS를 제거하기 전에 캡처.
	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
		{
			Flow->NotifyPlayerDisconnected(Exiting);
		}
	}

	// 떠난 플레이어가 점유했던 PlayerStart를 해제 → fallback(순번) 경로 슬롯 누수 방지.
	// 권위 슬롯 경로에선 슬롯이 고정이라 no-op이어도 무방.
	if (Exiting && Exiting->StartSpot.IsValid())
	{
		UsedStarts.Remove(Exiting->StartSpot);
	}
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	Super::Logout(Exiting);
}

void AD1BomberGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 매치 식별자/토큰/명단/봇 좌석 적재. 아무것도 없으면 PIE/standalone(결과 POST 스킵).
	MatchConfig = FD1MatchConfig::Load();

	// 맵 빌드는 전용 헬퍼로 위임(GameMode는 config만 보유·전달).
	FD1MapBuildConfig MapCfg;
	MapCfg.DefaultMap  = MapData;
	MapCfg.BlockZ      = BlockZ;
	MapCfg.PickupClass = PowerupPickupClass;
	MapCfg.DropChance  = PowerupDropChance;
	MapCfg.FireWeight  = FireDropWeight;
	MapCfg.BombWeight  = BombDropWeight;
	MapCfg.SpeedWeight = SpeedDropWeight;
	MapCfg.PowerupZ    = PowerupZ;
	FString MapErr;
	if (!UD1MapBuilder::Build(GetWorld(), GetGameState<AD1BomberGameState>(), MapCfg, MapErr))
	{
		UE_LOG(LogD1, Error, TEXT("[Map] 빌드 실패: %s"), *MapErr);
	}

	// 봇전: PlayerStart가 준비된(맵 빌드 후) 다음, 시작 게이트 전에 봇을 스폰해 PlayerArray를 채운다.
	SpawnBots();

	// 매치 흐름은 GameState의 컴포넌트가 소유. 설정을 넘기고 시작 게이트를 위임.
	// 명단도 함께 넘긴다 — 끝까지 입장하지 않은 유저를 결과에 채우려면 "와야 할 사람"을 알아야 한다.
	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
		{
			TArray<FD1JoinEntry> ExpectedRoster;
			MatchConfig.Roster.GenerateValueArray(ExpectedRoster);
			Flow->InitializeMatch(MatchConfig.ExpectedPlayers, WaitForPlayersTimeoutSec, ShutdownGraceSec,
				MatchConfig.MatchId, MatchConfig.MatchToken, ExpectedRoster);
		}
	}
}

FString AD1BomberGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	AD1BomberPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<AD1BomberPlayerState>() : nullptr;
	if (PS)
	{
		// 클라가 접속시 가져온 토큰(travel URL의 ?join=)이 백엔드가 준 roster에 있는지 확인.
		// 토큰이 roster에 존재 O -> PS에 UserId 넣어준다.
		// 토큰이 roster에 존재 X -> 해당 클라는 신용하지 않는다.(또는 PIE/StandAlone으로 판단)
		const FString JoinToken = UGameplayStatics::ParseOption(Options, TEXT("join"));
		if (!JoinToken.IsEmpty())
		{
			if (const FD1JoinEntry* Entry = MatchConfig.Roster.Find(JoinToken))
			{
				PS->SetBackendUserId(Entry->UserId);
				// Super가 클라 기본 이름(?Name=머신명)을 세팅한 뒤에 권위 닉네임으로 덮어써야 복제가 확정됨.
				// 너무 이르게(예: PlayerState::BeginPlay) 부르면 엔진이 되덮어 클라에 DESKTOP-… 잔류.
				if (!Entry->Nickname.IsEmpty())
				{
					PS->SetPlayerName(Entry->Nickname);
				}
				UE_LOG(LogD1, Log, TEXT("[Match] InitNewPlayer %s userId=%lld (roster)"),
					*PS->GetPlayerName(), Entry->UserId);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] InitNewPlayer %s — join 토큰이 roster에 없음"), *PS->GetPlayerName());
			}
		}
	}

	return Result;
}

AActor* AD1BomberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});

	TArray<AActor*> AllStarts;
	GetSortedPlayerStarts(this, AllStarts);

	TArray<AActor*> FreeStarts;
	FreeStarts.Reserve(AllStarts.Num());
	for (AActor* Start : AllStarts)
	{
		if (!UsedStarts.Contains(Start))
		{
			FreeStarts.Add(Start);
		}
	}

	if (FreeStarts.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	AActor* Chosen = FreeStarts[FMath::RandHelper(FreeStarts.Num())];

	// 슬롯 라벨 = 뽑힌 Start의 PlayerStartTag(없으면 정렬 인덱스). 스폰 코너·카드 자리가 함께 결정된다.
	if (AD1BomberPlayerState* BomberPS = Player ? Player->GetPlayerState<AD1BomberPlayerState>() : nullptr)
	{
		const int32 SlotIndex = ResolveSlotFromTag(Chosen, AllStarts.IndexOfByKey(Chosen));
		BomberPS->SetPlayerSlotIndex(SlotIndex);
		UE_LOG(LogD1, Log, TEXT("Slot %d -> Start %s (random) for %s"),
			SlotIndex, *Chosen->GetName(), *BomberPS->GetPlayerName());
	}

	UsedStarts.Add(Chosen);
	return Chosen;
}

void AD1BomberGameMode::SpawnBots()
{
	if (MatchConfig.Bots.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !BotControllerClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Bot] 스폰 생략 — World/BotControllerClass 없음"));
		return;
	}

	int32 Spawned = 0;
	for (const FD1JoinEntry& Bot : MatchConfig.Bots)
	{
		AController* BotController = World->SpawnActor<AController>(BotControllerClass);
		if (!BotController)
		{
			UE_LOG(LogD1, Warning, TEXT("[Bot] 컨트롤러 스폰 실패 userId=%lld"), Bot.UserId);
			continue;
		}

		// PlayerState는 컨트롤러 스폰 시 생성(bWantsPlayerState). 신원·이름·봇표시 stamp 후 폰 스폰·빙의.
		if (AD1BomberPlayerState* PS = BotController->GetPlayerState<AD1BomberPlayerState>())
		{
			PS->SetBackendUserId(Bot.UserId);
			if (!Bot.Nickname.IsEmpty())
			{
				PS->SetPlayerName(Bot.Nickname);
			}
			PS->SetIsBot(true);
		}

		// RestartPlayer가 ChoosePlayerStart(미사용 좌석 랜덤)로 슬롯 배정 + DefaultPawnClass 폰 스폰·빙의.
		RestartPlayer(BotController);
		Spawned++;
	}
	UE_LOG(LogD1, Log, TEXT("[Bot] 봇 %d명 스폰"), Spawned);
}
