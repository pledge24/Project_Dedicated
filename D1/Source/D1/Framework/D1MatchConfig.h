// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/D1MatchTypes.h"

/**
 *  DS 매치 설정 — 백엔드 설정 파일(JSON) 또는 커맨드라인 스위치에서 적재.
 *  전부 비어 있으면 PIE/standalone(결과 POST 생략).
 */
struct FD1MatchConfig
{
	/** 결과 POST 인증용(비면 생략 = PIE/standalone). */
	FString MatchId;
	FString ServerToken;

	/** 시작 정원(0/1=즉시). */
	int32 ExpectedPlayerCount = 0;

	/** join 토큰 → 신원. InitNewPlayer가 ?join= 토큰으로 신원 매핑. */
	TMap<FString, FD1JoinEntry> Roster;

	/** 봇 좌석(userId·닉네임). 비어 있으면 일반 매치. */
	TArray<FD1JoinEntry> Bots;

	/**
	 *  -MatchConfig= 파일이 있으면 그쪽, 없으면 커맨드라인 스위치(PIE·수동 실행).
	 *  백엔드가 띄운 DS는 설정 파일 경로만 받는다 — 토큰(결과 위조 권한)과 join 토큰(신원 도용 권한)이
	 *  커맨드라인에 실리면 같은 세션의 아무 프로세스나 읽을 수 있기 때문.
	 */
	static FD1MatchConfig Load();

private:
	/** 설정 파일(JSON) 파싱. 성공 시 true. 읽은 파일은 즉시 지운다 — 토큰이 디스크에 남는 창을 줄인다. */
	bool LoadFromFile(const FString& FilePath);

	/** -MatchConfig= 파일이 없을 때의 대체 경로(PIE·수동 실행) — -MatchId/-ServerToken/-Roster/-Bots 스위치에서 직접 파싱. */
	void LoadFromCommandLine();
};
