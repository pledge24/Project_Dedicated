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
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

	// PlayerStart의 Slot 반환. 태그가 0~3이 아니라면 DefaultSlot 반환
	int32 ResolveSlotFromTag(const AActor* Start, int32 DefaultSlot)
	{
		if (const APlayerStart* PlayerStart = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PlayerStart->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 TagNum = FCString::Atoi(*TagStr);
				if (0 <= TagNum && TagNum <= 3)
				{
					return TagNum;
				}
			}
		}
		return DefaultSlot;
	}

	// 커맨드라인 경로 전용 — 닉네임이 한글(비-ASCII)이라 백엔드가 표준 base64로 인코딩해 넘긴다.
	// (설정 파일 경로는 JSON이 UTF-8을 그대로 실어 나르므로 이 변환이 없다.)
	FString DecodeBase64Nickname(const FString& Encoded)
	{
		TArray<uint8> Bytes;
		if (!FBase64::Decode(Encoded, Bytes))
		{
			return FString();
		}

		Bytes.Add(0); // UTF8→TCHAR 변환용 널 종단

		return UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));
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

	const FD1JoinEntry* Entry = JoinRoster.Find(JoinToken);
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

	// 플레이어 입장 후속 처리는 매치 흐름 컴포넌트가 담당.
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
	LoadMatchConfig();

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
			JoinRoster.GenerateValueArray(ExpectedRoster);
			Flow->InitializeMatch(ExpectedPlayerCount, WaitForPlayersTimeoutSec, ShutdownGraceSec,
				CurrentMatchId, CurrentMatchToken, ExpectedRoster);
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
			if (const FD1JoinEntry* Entry = JoinRoster.Find(JoinToken))
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

void AD1BomberGameMode::LoadMatchConfig()
{
	// 백엔드가 띄운 DS는 설정 파일 경로만 받는다 — 토큰(결과 위조 권한)과 join 토큰(신원 도용 권한)이
	// 커맨드라인에 실리면 같은 세션의 아무 프로세스나 읽을 수 있기 때문.
	FString ConfigPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("MatchConfig="), ConfigPath) && !ConfigPath.IsEmpty())
	{
		if (LoadMatchConfigFromFile(ConfigPath))
		{
			return;
		}

		// 파일이 있는데 못 읽었다 = 백엔드가 띄운 DS인데 토큰이 없다. 결과 보고가 통째로 실패하므로 크게 남긴다.
		UE_LOG(LogD1, Error, TEXT("[Match] 매치 설정 파일 적재 실패 — 결과 보고 불가: %s"), *ConfigPath);
		return;
	}

	LoadMatchConfigFromCommandLine();
}

bool AD1BomberGameMode::LoadMatchConfigFromFile(const FString& FilePath)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		return false;
	}

	// 읽는 즉시 삭제 — 파일이 남아 있는 동안은 커맨드라인과 다를 바 없다.
	// (백엔드도 안전망 타이머로 지우지만, 정상 경로에서는 여기가 먼저다.)
	IFileManager::Get().Delete(*FilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	Root->TryGetStringField(TEXT("matchId"), CurrentMatchId);
	Root->TryGetStringField(TEXT("matchToken"), CurrentMatchToken);

	double ExpectedPlayers = 0.0;
	if (Root->TryGetNumberField(TEXT("expectedPlayers"), ExpectedPlayers))
	{
		ExpectedPlayerCount = static_cast<int32>(ExpectedPlayers);
	}

	const TArray<TSharedPtr<FJsonValue>>* RosterArray = nullptr;
	if (Root->TryGetArrayField(TEXT("roster"), RosterArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RosterArray)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Entry))
			{
				continue;
			}

			FString JoinToken;
			(*Entry)->TryGetStringField(TEXT("joinToken"), JoinToken);
			if (JoinToken.IsEmpty())
			{
				continue;
			}

			// 닉네임은 JSON(UTF-8)이 그대로 실어 나른다 — 커맨드라인 경로의 base64가 여기선 불필요.
			FD1JoinEntry JE;
			double UserId = 0.0;
			(*Entry)->TryGetNumberField(TEXT("userId"), UserId);
			JE.UserId = static_cast<int64>(UserId);
			(*Entry)->TryGetStringField(TEXT("nickname"), JE.Nickname);
			JoinRoster.Add(JoinToken, JE);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* BotsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("bots"), BotsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BotsArray)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Entry))
			{
				continue;
			}

			FD1JoinEntry Bot;
			double UserId = 0.0;
			(*Entry)->TryGetNumberField(TEXT("userId"), UserId);
			Bot.UserId = static_cast<int64>(UserId);
			(*Entry)->TryGetStringField(TEXT("nickname"), Bot.Nickname);
			PendingBots.Add(Bot);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d roster=%d bots=%d"),
		*CurrentMatchId, CurrentMatchToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"),
		ExpectedPlayerCount, JoinRoster.Num(), PendingBots.Num());

	return !CurrentMatchId.IsEmpty();
}

void AD1BomberGameMode::LoadMatchConfigFromCommandLine()
{
	FParse::Value(FCommandLine::Get(), TEXT("MatchId="), CurrentMatchId);
	FParse::Value(FCommandLine::Get(), TEXT("MatchToken="), CurrentMatchToken);
	FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayerCount);
	if (!CurrentMatchId.IsEmpty())
	{
		UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d (커맨드라인 경로)"),
			*CurrentMatchId, CurrentMatchToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"), ExpectedPlayerCount);
	}

	// {token}:{userId}:{base64(nickname)};… — InitNewPlayer가 ?join= 토큰으로 신원·이름을 확정한다.
	// 닉네임은 한글(비-ASCII)이라 Windows 커맨드라인 코드페이지 깨짐을 피하려 base64로 온다.
	FString RosterStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("Roster="), RosterStr) && !RosterStr.IsEmpty())
	{
		TArray<FString> Entries;
		RosterStr.ParseIntoArray(Entries, TEXT(";"), /*CullEmpty=*/true);
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.ParseIntoArray(Parts, TEXT(":"), /*CullEmpty=*/true);
			if (Parts.Num() >= 2)
			{
				FD1JoinEntry JE;
				JE.UserId = FCString::Atoi64(*Parts[1]);
				if (Parts.Num() >= 3)
				{
					JE.Nickname = DecodeBase64Nickname(Parts[2]);
				}
				JoinRoster.Add(Parts[0], JE);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] roster 항목 형식 오류(무시): '%s'"), *Entry);
			}
		}
		UE_LOG(LogD1, Log, TEXT("[Match] roster 주입 %d명"), JoinRoster.Num());
	}

	// userId:base64(nickname);… — 토큰 없음(DS가 서버측 스폰). userId는 음수 sentinel.
	FString BotsStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("Bots="), BotsStr) && !BotsStr.IsEmpty())
	{
		TArray<FString> Entries;
		BotsStr.ParseIntoArray(Entries, TEXT(";"), /*CullEmpty=*/true);
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.ParseIntoArray(Parts, TEXT(":"), /*CullEmpty=*/true);
			if (Parts.Num() < 1)
			{
				UE_LOG(LogD1, Warning, TEXT("[Bot] 항목 형식 오류(무시): '%s'"), *Entry);
				continue;
			}

			FD1JoinEntry Bot;
			Bot.UserId = FCString::Atoi64(*Parts[0]);
			if (Parts.Num() >= 2)
			{
				Bot.Nickname = DecodeBase64Nickname(Parts[1]);
			}
			PendingBots.Add(Bot);
		}
	}
}

void AD1BomberGameMode::SpawnBots()
{
	// 봇 좌석이 없으면 일반 매치 → 봇 스폰 없음.
	if (PendingBots.Num() == 0)
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
	for (const FD1JoinEntry& Bot : PendingBots)
	{
		// 봇 생성 시작.
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
