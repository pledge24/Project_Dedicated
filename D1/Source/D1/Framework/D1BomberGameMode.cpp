// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1BotController.h"
#include "Framework/D1MatchFlowComponent.h"
#include "Framework/D1PlayerRemovalComponent.h"
#include "Systems/Map/D1MapBuilder.h"
#include "Systems/Map/D1MapData.h"
#include "Framework/D1MatchTypes.h"
#include "Network/D1DsShutdownSubsystem.h"
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

void AD1BomberGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// 매치 식별자/토큰/명단/봇 좌석 적재. 아무것도 없으면 PIE/standalone(결과 POST 생략).
	// PreLogin·InitNewPlayer가 이 값으로 접속 신원을 판정하므로 접속 수락보다 앞서 채워야 한다 —
	// 비어 있으면 실 DS를 PIE로 오판해 신원 검증이 통째로 열린다.
	MatchConfig = FD1MatchConfig::Load();
}

void AD1BomberGameMode::InitGameState()
{
	Super::InitGameState();

	// 매치 흐름·킥·탈주는 GameState의 컴포넌트가 소유. 여기선 설정만 주입하고 가동(준비 통지·게이트·
	// 폴링)은 맵과 봇이 준비된 BeginPlay에서 건다.
	// 여기서 조용히 넘어가면 시작 게이트도 킥 폴링도 결과 보고도 걸리지 않은 DS가
	// Waiting 상태로 영원히 남는다. GameStateClass를 잘못 지정했을 때 여기서 걸린다.
	AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	if (!ensureMsgf(GS, TEXT("[Match] GameStateClass가 AD1BomberGameState 계열이 아님")))
	{
		return;
	}

	// 명단도 함께 넘긴다 — 끝까지 입장하지 않은 유저를 결과에 채우려면 "와야 할 사람"을 알아야 한다.
	FD1MatchSetupParams Params;
	Params.ExpectedPlayerCount      = MatchConfig.ExpectedPlayerCount;
	Params.MatchId                  = MatchConfig.MatchId;
	Params.ServerToken              = MatchConfig.ServerToken;
	Params.WaitForPlayersTimeoutSec = WaitForPlayersTimeoutSec;
	Params.ShutdownGraceSec         = ShutdownGraceSec;
	MatchConfig.Roster.GenerateValueArray(Params.ExpectedRoster);

	if (UD1PlayerRemovalComponent* Removal = GS->GetPlayerRemoval())
	{
		Removal->SetupForMatch(Params);
	}

	if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
	{
		Flow->SetupForMatch(Params);
	}
}

void AD1BomberGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// 매치 시작 후 입장 거절 — 신규·복귀 불문(재입장 허용 창은 시작 전까지).
	// 늦은 좌석은 StartMatch가 이미 미입장자로 확정했다. PC를 스폰하기 전에 끊는 빠른 경로다.
	if (IsJoinAfterMatchStart())
	{
		ErrorMessage = TEXT("매치가 이미 시작되어 입장할 수 없습니다.");
		UE_LOG(LogD1, Warning, TEXT("[Match] PreLogin 거절 — 매치 시작 후 입장 시도 (%s)"), *Address);
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
		if (const UD1PlayerRemovalComponent* Removal = GS->GetPlayerRemoval())
		{
			if (Removal->IsUserKicked(Entry->UserId))
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
			Flow->NotifyPlayerJoined();
		}
	}
}

void AD1BomberGameMode::Logout(AController* Exiting)
{
	// 매치 진행 중 이탈(접속 끊김/나가기)은 탈주로 처리 — Super가 PS를 제거하기 전에 기록.
	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (UD1PlayerRemovalComponent* Removal = GS->GetPlayerRemoval())
		{
			Removal->NotifyPlayerDisconnected(Exiting);
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

		// 실 DS는 벽·스폰 없는 맵으로 매치를 돌릴 수 없다 — 결과 보고 없이 종료하면 백엔드 sweep이
		// 점수 무변동 abort로 기록한다(/result는 빈 결과를 거부하므로 결과를 지어내지 않는다).
		// 유예 중 입장한 클라는 종료와 함께 끊긴다. PIE/standalone은 빈 맵 관찰을 위해 계속 진행.
		if (!MatchConfig.MatchId.IsEmpty() && !MatchConfig.ServerToken.IsEmpty())
		{
			if (UD1DsShutdownSubsystem* Shutdown = GetWorld()->GetSubsystem<UD1DsShutdownSubsystem>())
			{
				Shutdown->BeginShutdownWatch(ShutdownGraceSec);
			}

			return;
		}
	}

	// 봇전: PlayerStart가 준비된(맵 빌드 후) 다음, 시작 게이트 전에 봇을 스폰해 PlayerArray를 채운다.
	SpawnBots();

	// 설정 주입은 InitGameState에서 끝났다 — 여기선 가동만. 맵·PlayerStart·봇이 준비된 뒤라야
	// 준비 통지가 사실이 되고, 정원 0/1(PIE·솔로)의 즉시 시작이 빈 월드에서 일어나지 않는다.
	AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	if (!ensureMsgf(GS, TEXT("[Match] 매치 가동 불가 — GameState 없음")))
	{
		return;
	}

	if (UD1PlayerRemovalComponent* Removal = GS->GetPlayerRemoval())
	{
		Removal->StartKickPolling();
	}

	if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
	{
		Flow->StartMatchGate();
	}
}

FString AD1BomberGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	// PreLogin과 같은 판정을 한 번 더 — PreLogin은 클라의 join 응답이 오기 전에 돌아서, 그 왕복 사이에
	// StartMatch가 끼면 이미 승인된 접속이 시작 후에 들어온다(게이트 타임아웃 직전 접속이 이 창에 걸린다).
	// Super보다 앞에 둬야 UserId가 안 찍혀, PC 파괴가 부르는 Logout이 이 접속을 탈주로 오정산하지 않는다.
	if (IsJoinAfterMatchStart())
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] InitNewPlayer 거절 — PreLogin 승인 후 매치가 시작됨"));

		return TEXT("매치가 이미 시작되어 입장할 수 없습니다.");
	}

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

bool AD1BomberGameMode::IsJoinAfterMatchStart() const
{
	// 토큰 없는 PIE/standalone은 ExpectedPlayerCount<=1로 BeginPlay 중 즉시 Playing이 된다 —
	// 여기서 막으면 추가 PIE 클라가 통째로 못 들어온다.
	if (MatchConfig.ServerToken.IsEmpty())
	{
		return false;
	}

	return GetGameState<AD1BomberGameState>()->GetMatchPhase() != EBomberMatchPhase::Waiting;
}

void AD1BomberGameMode::SpawnBots()
{
	if (MatchConfig.Bots.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!BotControllerClass)
	{
		UE_LOG(LogD1, Warning, TEXT("[Bot] 스폰 생략 — BotControllerClass 없음"));
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
