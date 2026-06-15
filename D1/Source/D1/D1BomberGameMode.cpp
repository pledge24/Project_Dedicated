// Copyright Epic Games, Inc. All Rights Reserved.

#include "D1BomberGameMode.h"
#include "D1BomberGameState.h"
#include "D1BomberPlayerState.h"
#include "D1BomberGridLibrary.h"
#include "D1MapData.h"
#include "D1MatchTypes.h"
#include "D1PowerupPickup.h"
#include "D1SoftBlock.h"
#include "D1WallBlock.h"
#include "D1.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Online/BackendSubsystem.h"

namespace
{
	const TCHAR* EndReasonToString(EBomberEndReason Reason)
	{
		switch (Reason)
		{
		case EBomberEndReason::Winner:      return TEXT("winner");
		case EBomberEndReason::Draw:        return TEXT("draw");
		case EBomberEndReason::TimeExpired: return TEXT("time_expired");
		default:                            return TEXT("abort");
		}
	}

	// 슬롯 번호 → PlayerStart. PlayerStartTag=="N" 우선, 없으면 이름순 정렬의 N번째.
	AActor* FindStartForSlot(const UObject* WorldContext, int32 Slot)
	{
		if (Slot < 0)
		{
			return nullptr;
		}

		TArray<AActor*> AllStarts;
		UGameplayStatics::GetAllActorsOfClass(WorldContext, APlayerStart::StaticClass(), AllStarts);
		AllStarts.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetName() < B.GetName();
		});

		const FString SlotTag = FString::FromInt(Slot);
		for (AActor* Start : AllStarts)
		{
			if (const APlayerStart* PS = Cast<APlayerStart>(Start))
			{
				if (PS->PlayerStartTag.ToString() == SlotTag)
				{
					return Start;
				}
			}
		}

		return AllStarts.IsValidIndex(Slot) ? AllStarts[Slot] : nullptr;
	}
}

AD1BomberGameMode::AD1BomberGameMode()
{
	GameStateClass = AD1BomberGameState::StaticClass();
	PlayerStateClass = AD1BomberPlayerState::StaticClass();
}

void AD1BomberGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 백엔드가 spawn 시 주입한 매치 식별자/토큰/예상 인원. 둘 다 없으면 PIE/standalone(결과 POST 스킵).
	FParse::Value(FCommandLine::Get(), TEXT("MatchId="), CurrentMatchId);
	FParse::Value(FCommandLine::Get(), TEXT("MatchToken="), CurrentMatchToken);
	FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayerCount);
	if (!CurrentMatchId.IsEmpty())
	{
		UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d"),
			*CurrentMatchId, CurrentMatchToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"), ExpectedPlayerCount);
	}

	BuildMapFromData();

	// 시작 게이트: 예상 인원 0/1(PIE·솔로)이면 즉시 시작, 아니면 전원 입장(PostLogin) 또는 타임아웃까지 Waiting.
	if (ExpectedPlayerCount <= 1)
	{
		StartMatch();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			WaitForPlayersTimerHandle, this, &AD1BomberGameMode::OnWaitForPlayersTimeout,
			WaitForPlayersTimeoutSec, /*bLoop=*/false);
		UE_LOG(LogD1, Log, TEXT("[Match] 시작 게이트 대기 — 예상 %d명 (타임아웃 %.0fs)"),
			ExpectedPlayerCount, WaitForPlayersTimeoutSec);
	}
}

AActor* AD1BomberGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UsedStarts.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr) { return !Ptr.IsValid(); });

	TArray<AActor*> AllStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), AllStarts);

	// 액터 이름 알파벳 정렬 — 슬롯 인덱스 일관성 확보.
	AllStarts.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});

	AD1BomberPlayerState* BomberPS = Player ? Player->GetPlayerState<AD1BomberPlayerState>() : nullptr;

	// 백엔드 권위 슬롯(InitNewPlayer가 ?slot= 로 세팅)이 있으면 그 슬롯 자리로 고정 배치.
	// userId별 슬롯이 고정되므로 이중접속/유령 연결이 있어도 색·위치가 안 꼬인다.
	if (BomberPS && BomberPS->PlayerSlotIndex >= 0)
	{
		// 슬롯→Start는 공용 헬퍼로. (단 DS에선 이 함수가 InitNewPlayer보다 먼저 돌아
		//  보통 PlayerSlotIndex<0 → 아래 fallback로 빠지고, 위치는 PostLogin에서 보정한다.)
		if (AActor* Chosen = FindStartForSlot(this, BomberPS->PlayerSlotIndex))
		{
			UsedStarts.Add(Chosen);
			UE_LOG(LogD1, Log, TEXT("Slot %d -> Start %s (authoritative) for %s"),
				BomberPS->PlayerSlotIndex, *Chosen->GetName(), *BomberPS->GetPlayerName());
			return Chosen;
		}

		UE_LOG(LogD1, Warning, TEXT("Slot %d has no matching PlayerStart; falling back"), BomberPS->PlayerSlotIndex);
	}

	// fallback: 슬롯 미지정(PIE/standalone, 백엔드 없음) — 안 쓴 Start를 순번대로 배정.
	for (int32 i = 0; i < AllStarts.Num(); ++i)
	{
		AActor* Start = AllStarts[i];

		bool bAlreadyUsed = false;
		for (const TWeakObjectPtr<AActor>& Used : UsedStarts)
		{
			if (Used.Get() == Start)
			{
				bAlreadyUsed = true;
				break;
			}
		}
		if (bAlreadyUsed)
		{
			continue;
		}

		// 슬롯 인덱스 결정: PlayerStartTag가 "0"~"3"이면 그 값, 아니면 정렬 인덱스.
		int32 SlotIndex = i;
		if (APlayerStart* PS = Cast<APlayerStart>(Start))
		{
			const FString TagStr = PS->PlayerStartTag.ToString();
			if (TagStr.IsNumeric())
			{
				const int32 Parsed = FCString::Atoi(*TagStr);
				if (Parsed >= 0 && Parsed <= 3)
				{
					SlotIndex = Parsed;
				}
			}
		}

		// PlayerState에 슬롯 부여.
		if (BomberPS)
		{
			BomberPS->SetPlayerSlotIndex(SlotIndex);
			UE_LOG(LogD1, Log, TEXT("Assigned PlayerSlotIndex=%d to %s (Start=%s)"),
				SlotIndex, *BomberPS->GetPlayerName(), *Start->GetName());
		}

		UsedStarts.Add(Start);
		return Start;
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

FString AD1BomberGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	AD1BomberPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<AD1BomberPlayerState>() : nullptr;
	if (PS)
	{
		// travel URL의 ?userId= 를 PlayerState에 보관 → 매치 종료 시 결과 POST에 사용.
		const FString UserIdStr = UGameplayStatics::ParseOption(Options, TEXT("userId"));
		if (!UserIdStr.IsEmpty())
		{
			PS->BackendUserId = FCString::Atoi64(*UserIdStr);
			UE_LOG(LogD1, Log, TEXT("[Match] InitNewPlayer %s userId=%lld"), *PS->GetPlayerName(), PS->BackendUserId);
		}

		// travel URL의 ?slot= 을 백엔드 권위 슬롯으로 채택 → ChoosePlayerStart가 이 자리로 배치.
		const FString SlotStr = UGameplayStatics::ParseOption(Options, TEXT("slot"));
		if (SlotStr.IsNumeric())
		{
			const int32 Slot = FCString::Atoi(*SlotStr);
			if (Slot >= 0 && Slot <= 3)
			{
				PS->SetPlayerSlotIndex(Slot);
				UE_LOG(LogD1, Log, TEXT("[Match] InitNewPlayer %s slot=%d"), *PS->GetPlayerName(), Slot);
			}
		}
	}

	return Result;
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

void AD1BomberGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// 슬롯 기반 스폰 위치 보정. DS에선 ChoosePlayerStart가 InitNewPlayer보다 먼저 돌아
	// 권위 슬롯을 모른 채 fallback 위치로 스폰된다. 슬롯이 확정된 지금(InitNewPlayer 이후) 슬롯 자리로 옮긴다.
	if (NewPlayer)
	{
		if (const AD1BomberPlayerState* PS = NewPlayer->GetPlayerState<AD1BomberPlayerState>())
		{
			if (APawn* Pawn = NewPlayer->GetPawn())
			{
				if (const AActor* Start = FindStartForSlot(this, PS->PlayerSlotIndex))
				{
					Pawn->SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
				}
			}
		}
	}

	// 이미 시작했거나 게이트 비활성(PIE·솔로)이면 시작 게이트 카운트 생략.
	if (bMatchStarted || ExpectedPlayerCount <= 1)
	{
		return;
	}

	int32 Connected = 0;
	if (const AGameStateBase* GS = GameState)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (Cast<AD1BomberPlayerState>(PS))
			{
				++Connected;
			}
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 입장 %d/%d"), Connected, ExpectedPlayerCount);

	if (Connected >= ExpectedPlayerCount)
	{
		StartMatch();
	}
}

void AD1BomberGameMode::NotifyPlayerDied(AD1BomberPlayerState* DeadPS)
{
	if (bMatchEnded || !DeadPS)
	{
		return;
	}

	EnsureAliveListInitialized();

	if (DeadPS->Placement <= 0)
	{
		// 등수 = 죽는 시점의 생존자 수(자기 포함).
		DeadPS->Placement = AlivePlayerStates.Num();
		AlivePlayerStates.Remove(DeadPS);
		UE_LOG(LogD1, Log, TEXT("Player died: %s Placement=%d Remaining=%d"),
			*DeadPS->GetPlayerName(), DeadPS->Placement, AlivePlayerStates.Num());
	}

	if (AlivePlayerStates.Num() <= 1)
	{
		const bool bHasSurvivor = AlivePlayerStates.Num() == 1;
		AD1BomberPlayerState* Winner = bHasSurvivor ? AlivePlayerStates[0].Get() : DeadPS;
		EndMatchWithWinner(Winner, bHasSurvivor ? EBomberEndReason::Winner : EBomberEndReason::Draw);
	}
}

bool AD1BomberGameMode::RollPowerupType(EPowerupType& OutType) const
{
	if (FMath::FRand() > PowerupDropChance)
	{
		return false;
	}

	const int32 TotalWeight = FireDropWeight + BombDropWeight + SpeedDropWeight;
	if (TotalWeight <= 0)
	{
		return false;
	}

	const int32 Roll = FMath::RandRange(0, TotalWeight - 1);
	if (Roll < FireDropWeight)
	{
		OutType = EPowerupType::Fire;
	}
	else if (Roll < FireDropWeight + BombDropWeight)
	{
		OutType = EPowerupType::Bomb;
	}
	else
	{
		OutType = EPowerupType::Speed;
	}
	return true;
}

void AD1BomberGameMode::BuildMapFromData()
{
	AD1BomberGameState* BomberGS = GetGameState<AD1BomberGameState>();
	UWorld* World = GetWorld();
	if (!BomberGS || !World)
	{
		UE_LOG(LogD1, Warning, TEXT("[Map] GameState/World 없음 — 맵 빌드 스킵"));
		return;
	}

	// 맵 선택: -MapData= 커맨드라인 오버라이드 우선(백엔드 주입/맵 스왑), 없으면 BP 기본값.
	const UD1MapData* MapToUse = MapData;
	FString MapPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("MapData="), MapPath) && !MapPath.IsEmpty())
	{
		if (UD1MapData* Loaded = LoadObject<UD1MapData>(nullptr, *MapPath))
		{
			MapToUse = Loaded;
			UE_LOG(LogD1, Log, TEXT("[Map] -MapData= 오버라이드: %s"), *MapPath);
		}
		else
		{
			UE_LOG(LogD1, Warning, TEXT("[Map] -MapData= 로드 실패: %s — 기본값 사용"), *MapPath);
		}
	}

	if (!MapToUse)
	{
		UE_LOG(LogD1, Error, TEXT("[Map] MapData 미지정 — 맵을 빌드할 수 없음"));
		return;
	}

	FD1MapLayout Layout;
	FString Err;
	if (!MapToUse->BuildLayout(Layout, Err))
	{
		UE_LOG(LogD1, Error, TEXT("[Map] 파싱 실패: %s"), *Err);
		return;
	}

	// 폭발/경계 판정 권위 데이터(클라에도 복제).
	BomberGS->GridSize = Layout.GridSize;
	BomberGS->WallCells = Layout.WallCells;
	BomberGS->SoftBlockCells = Layout.SoftBlockCells;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 벽(복제) — 메시·콜리전은 액터 생성자에 있어 클라도 동일 구성.
	if (MapToUse->WallBlockClass)
	{
		for (const FIntPoint& Cell : Layout.WallCells)
		{
			World->SpawnActor<AD1WallBlock>(MapToUse->WallBlockClass,
				UD1BomberGridLibrary::CellToWorldCenter(Cell, BlockZ), FRotator::ZeroRotator, Params);
		}
	}

	// 소프트블록(복제) — dying 상태는 자체 복제. 빌드 시 보유 아이템 사전 배정.
	int32 AssignedItems = 0;
	if (MapToUse->SoftBlockClass)
	{
		for (const FIntPoint& Cell : Layout.SoftBlockCells)
		{
			AD1SoftBlock* Block = World->SpawnActor<AD1SoftBlock>(MapToUse->SoftBlockClass,
				UD1BomberGridLibrary::CellToWorldCenter(Cell, BlockZ), FRotator::ZeroRotator, Params);
			if (Block)
			{
				// 확률·가중치는 그대로, 굴리는 시점만 빌드로. 파괴 시엔 굴리지 않고 이걸 스폰.
				EPowerupType HeldType;
				if (RollPowerupType(HeldType))
				{
					Block->SetHeldItem(HeldType, PowerupPickupClass, PowerupZ);
					++AssignedItems;
				}
			}
		}
	}

	// 스폰 지점(서버 전용) — PlayerStartTag=슬롯 → 기존 FindStartForSlot 태그 경로가 그대로 동작.
	for (const FD1MapStart& Start : Layout.Starts)
	{
		const FVector Loc = UD1BomberGridLibrary::CellToWorldCenter(Start.Cell, 0.f);
		if (APlayerStart* PS = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), Loc, FRotator::ZeroRotator, Params))
		{
			PS->PlayerStartTag = FName(*FString::FromInt(Start.Slot));
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Map] 빌드 완료 — %dx%d, 벽 %d, 소프트 %d, 스폰 %d, 아이템 %d"),
		Layout.GridSize.X, Layout.GridSize.Y,
		Layout.WallCells.Num(), Layout.SoftBlockCells.Num(), Layout.Starts.Num(), AssignedItems);
}

void AD1BomberGameMode::EndMatchWithWinner(AD1BomberPlayerState* WinnerPS, EBomberEndReason Reason)
{
	if (bMatchEnded)
	{
		return;
	}
	bMatchEnded = true;

	if (WinnerPS && WinnerPS->Placement <= 0)
	{
		WinnerPS->Placement = 1;
	}

	AD1BomberGameState* GS = GetGameState<AD1BomberGameState>();
	if (GS)
	{
		GS->MatchPhase = EBomberMatchPhase::Finished;
	}

	UE_LOG(LogD1, Log, TEXT("Match ended (%s). Winner=%s (Placement=%d)"),
		EndReasonToString(Reason),
		WinnerPS ? *WinnerPS->GetPlayerName() : TEXT("(none)"),
		WinnerPS ? WinnerPS->Placement : 0);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->DisableInput(PC);
		}
	}

	if (!GS)
	{
		return;
	}

	// 최종 결과 스냅샷(UI 원자 복제) + 백엔드 보고용 수집을 한 번에.
	TArray<FD1MatchResultEntry> Entries;
	TArray<FMatchResultPlayer> ResultPlayers;
	Entries.Reserve(GS->PlayerArray.Num());
	ResultPlayers.Reserve(GS->PlayerArray.Num());
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
		{
			// 미배정 생존자(시간 만료/무승부)는 공동 1위로 보정 — 백엔드는 placement 1~N만 허용.
			if (B->Placement <= 0)
			{
				B->Placement = 1;
			}

			FD1MatchResultEntry Entry;
			Entry.Placement = B->Placement;
			Entry.Nickname  = B->GetPlayerName();
			Entry.SlotIndex = B->PlayerSlotIndex;
			Entry.LivesLeft = B->Lives;
			Entries.Add(Entry);

			FMatchResultPlayer RP;
			RP.UserId    = B->BackendUserId;
			RP.Placement = B->Placement;
			RP.LivesLeft = B->Lives;
			ResultPlayers.Add(RP);
		}
	}
	GS->SetFinalResults(Entries);

	// 백엔드가 띄운 DS일 때만 결과 보고(토큰 없으면 PIE/standalone → 스킵).
	if (!CurrentMatchToken.IsEmpty())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UBackendSubsystem* Backend = GI->GetSubsystem<UBackendSubsystem>())
			{
				const int32 DurationSec = FMath::Max(0,
					FMath::RoundToInt(GS->GetServerWorldTimeSeconds() - GS->MatchStartServerTime));
				Backend->ReportMatchResult(CurrentMatchId, CurrentMatchToken, GetWorld()->GetMapName(),
					DurationSec, EndReasonToString(Reason), ResultPlayers);
			}
		}
	}

	// 클라들이 결과 화면 카운트다운 후 ClientTravel로 빠지면 DS가 스스로 종료.
	StartShutdownWatchdog();
}

void AD1BomberGameMode::EnsureAliveListInitialized()
{
	if (AlivePlayerStates.Num() > 0)
	{
		return;
	}
	if (AGameStateBase* GSB = GameState)
	{
		for (APlayerState* PS : GSB->PlayerArray)
		{
			if (AD1BomberPlayerState* B = Cast<AD1BomberPlayerState>(PS))
			{
				// ApplyHit가 NotifyPlayerDied보다 먼저 bIsAlive를 꺼서, 첫 사망자가
				// 누락되면 등수가 1 모자람. 미랭크(Placement<=0) 기준으로 전원 포함.
				if (B->Placement <= 0)
				{
					AlivePlayerStates.Add(B);
				}
			}
		}
	}
}

void AD1BomberGameMode::StartMatch()
{
	if (bMatchStarted)
	{
		return;
	}
	bMatchStarted = true;
	GetWorldTimerManager().ClearTimer(WaitForPlayersTimerHandle);

	if (AD1BomberGameState* BomberGS = GetGameState<AD1BomberGameState>())
	{
		BomberGS->MatchStartServerTime = BomberGS->GetServerWorldTimeSeconds();
		BomberGS->MatchPhase = EBomberMatchPhase::Playing;

		// 매치 제한시간 만료 콜백.
		GetWorldTimerManager().SetTimer(
			MatchTimerHandle, this, &AD1BomberGameMode::OnMatchTimeExpired,
			BomberGS->MatchDurationSec, /*bLoop=*/false);
	}

	UE_LOG(LogD1, Log, TEXT("[Match] 매치 시작 (Playing)"));
}

void AD1BomberGameMode::OnWaitForPlayersTimeout()
{
	if (bMatchStarted)
	{
		return;
	}
	UE_LOG(LogD1, Warning, TEXT("[Match] 시작 게이트 타임아웃 — 현재 인원으로 시작"));
	StartMatch();
}

void AD1BomberGameMode::OnMatchTimeExpired()
{
	if (bMatchEnded)
	{
		return;
	}
	UE_LOG(LogD1, Log, TEXT("Match time expired -> ending match"));
	// 생존자는 EndMatchWithWinner에서 공동 1위로 보정된다.
	EndMatchWithWinner(nullptr, EBomberEndReason::TimeExpired);
}

void AD1BomberGameMode::StartShutdownWatchdog()
{
	// PIE/Listen 서버는 에디터를 죽이면 안 됨 — 실 DS에서만 자가 종료.
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	ShutdownElapsed = 0.f;
	GetWorldTimerManager().SetTimer(
		ShutdownWatchdogHandle, this, &AD1BomberGameMode::TickShutdownWatchdog, 1.f, /*bLoop=*/true);
	UE_LOG(LogD1, Log, TEXT("[Match] 종료 감시 시작 — 전원 퇴장 또는 %.0fs 후 DS 종료"), ShutdownGraceSec);
}

void AD1BomberGameMode::TickShutdownWatchdog()
{
	// 클라들이 ClientTravel로 빠지면 Logout → NumPlayers 감소. 0 도달 시 정상 종료.
	if (GetNumPlayers() <= 0)
	{
		UE_LOG(LogD1, Log, TEXT("[Match] 전원 퇴장 — DS 종료"));
		RequestServerShutdown();
		return;
	}

	ShutdownElapsed += 1.f;
	if (ShutdownElapsed >= ShutdownGraceSec)
	{
		UE_LOG(LogD1, Warning, TEXT("[Match] 종료 하드캡(%.0fs) 도달 — 잔류 클라 무시하고 DS 종료"), ShutdownGraceSec);
		RequestServerShutdown();
	}
}

void AD1BomberGameMode::RequestServerShutdown()
{
	GetWorldTimerManager().ClearTimer(ShutdownWatchdogHandle);
	UE_LOG(LogD1, Log, TEXT("[Match] DS 프로세스 종료 요청(RequestExit)"));
	FPlatformMisc::RequestExit(false);
}
