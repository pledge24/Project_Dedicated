// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/D1MatchConfig.h"

#include "Core/D1LogChannels.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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

FD1MatchConfig FD1MatchConfig::Load()
{
	FD1MatchConfig Config;

	FString ConfigPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("MatchConfig="), ConfigPath) && !ConfigPath.IsEmpty())
	{
		if (!Config.LoadFromFile(ConfigPath))
		{
			// 파일이 있는데 못 읽었다 = 백엔드가 띄운 DS인데 토큰이 없다. 결과 보고가 통째로 실패하므로 크게 남긴다.
			UE_LOG(LogD1, Error, TEXT("[Match] 매치 설정 파일 적재 실패 — 결과 보고 불가: %s"), *ConfigPath);
		}

		return Config;
	}

	Config.LoadFromCommandLine();

	return Config;
}

bool FD1MatchConfig::LoadFromFile(const FString& FilePath)
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

	Root->TryGetStringField(TEXT("matchId"), MatchId);
	Root->TryGetStringField(TEXT("serverToken"), ServerToken);

	double ExpectedPlayersValue = 0.0;
	if (Root->TryGetNumberField(TEXT("expectedPlayers"), ExpectedPlayersValue))
	{
		ExpectedPlayerCount = static_cast<int32>(ExpectedPlayersValue);
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
			Roster.Add(JoinToken, JE);
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
			Bots.Add(Bot);
		}
	}

	UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d roster=%d bots=%d"),
		*MatchId, ServerToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"),
		ExpectedPlayerCount, Roster.Num(), Bots.Num());

	return !MatchId.IsEmpty();
}

void FD1MatchConfig::LoadFromCommandLine()
{
	FParse::Value(FCommandLine::Get(), TEXT("MatchId="), MatchId);
	FParse::Value(FCommandLine::Get(), TEXT("ServerToken="), ServerToken);
	FParse::Value(FCommandLine::Get(), TEXT("ExpectedPlayers="), ExpectedPlayerCount);
	if (!MatchId.IsEmpty())
	{
		UE_LOG(LogD1, Log, TEXT("[Match] DS matchId=%s token=%s expected=%d (커맨드라인 경로)"),
			*MatchId, ServerToken.IsEmpty() ? TEXT("(none)") : TEXT("(set)"), ExpectedPlayerCount);
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
				Roster.Add(Parts[0], JE);
			}
			else
			{
				UE_LOG(LogD1, Warning, TEXT("[Match] roster 항목 형식 오류(무시): '%s'"), *Entry);
			}
		}
		UE_LOG(LogD1, Log, TEXT("[Match] roster 주입 %d명"), Roster.Num());
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
			Bots.Add(Bot);
		}
	}
}
