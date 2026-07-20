// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1BomberGameMode.h"
#include "Framework/D1BomberGameState.h"
#include "Framework/D1BomberPlayerState.h"
#include "Framework/D1MatchFlowComponent.h"
#include "Systems/Map/D1MapBuilder.h"
#include "Systems/Map/D1MapData.h"
#include "Framework/D1MatchTypes.h"
#include "Core/D1LogChannels.h"
#include "EngineUtils.h"
#include "Misc/Base64.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// 이름순 정렬 — 슬롯 인덱스 일관성 확보.
	void GatherSortedPlayerStarts(const UObject* WorldContext, TArray<AActor*>& OutStarts)
	{
		UGameplayStatics::GetAllActorsOfClass(WorldContext, APlayerStart::StaticClass(), OutStarts);
		OutStarts.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetName() < B.GetName();
		});
	}

	// PlayerStartTag가 "0"~"3"이면 그 슬롯, 아니면 DefaultSlot 유지.
	int32 ResolveSlotFromTag(const AActor* Start, int32 DefaultSlot)
	{
		if (const APlayerStart* PS = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PS->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 Parsed = FCString::Atoi(*TagStr);
				if (Parsed >= 0 && Parsed <= 3)
				{
					return Parsed;
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

	// 시작 게이트 카운트는 매치 흐름 컴포넌트가 담당.
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

	// DS spawn 시 백엔드가 주입한 커맨드라인에서 매치 식별자/토큰/예상 인원 파싱. 없으면 PIE/standalone(결과 POST 스킵).
	FParse::Value(FCommandLine::Get(), TEXT("MatchId="), CurrentMatchId);
	FParse::Value(FCommandLine::Get(), TEXT("MatchToken="), CurrentMatchToken);
	FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayerCount);
	if (!CurrentMatchId.IsEmpty())
	{
		UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d"),
			*CurrentMatchId, CurrentMatchToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"), ExpectedPlayerCount);
	}

	// DS spawn 시 백엔드가 주입한 커맨드라인에서 플레이어 명단 파싱.
	// ({token}:{userId}:{base64(nickname)};…). InitNewPlayer가 ?join= 토큰으로 신원·이름을 확정한다.
	// 닉네임은 한글(비-ASCII)이라 백엔드가 표준 base64로 인코딩해 넘김 → UTF-8로 디코드.
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
					TArray<uint8> Bytes;
					if (FBase64::Decode(Parts[2], Bytes))
					{
						Bytes.Add(0); // UTF8→TCHAR 변환용 널 종단
						JE.Nickname = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));
					}
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

	// 매치 흐름은 GameState의 컴포넌트가 소유. 설정을 넘기고 시작 게이트를 위임.
	if (AD1BomberGameState* GS = GetGameState<AD1BomberGameState>())
	{
		if (UD1MatchFlowComponent* Flow = GS->GetMatchFlow())
		{
			Flow->InitializeMatch(ExpectedPlayerCount, WaitForPlayersTimeoutSec, ShutdownGraceSec,
				CurrentMatchId, CurrentMatchToken);
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
	GatherSortedPlayerStarts(this, AllStarts);

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
