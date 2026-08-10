// DS에 넘길 매치 설정 파일 — 커맨드라인에 두면 안 되는 값(토큰)을 임시 파일로 전달한다.
// ds.ts에서 분리한 이유: 비밀값을 다루는 코드가 프로세스 spawn·포트 관리와 섞이면
// 토큰 취급이 바뀔 때마다 프로세스 수명 코드까지 리뷰 범위에 들어온다.
import { existsSync, unlinkSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import { config } from '../../common/config.js';
import { logger } from '../../common/logger.js';

/** DS에 넘길 매치 설정. 커맨드라인에 두면 안 되는 값(토큰)이 전부 여기 모인다. */
export interface MatchConfigFile
{
    matchId: string;
    serverToken: string;
    expectedPlayers: number;
    roster: { joinToken: string; userId: number; nickname: string }[];
    bots: { userId: number; nickname: string }[];
}

const ds = config.match.ds;
// 이 프로세스가 쓴 매치 설정 파일. 지워지면(DS가 읽고 삭제·안전망 타이머·명시 정리) 빠진다.
const writtenConfigPaths = new Set<string>();

/**
 * 설정 파일을 임시 디렉터리에 쓰고 경로 반환.
 * mode 0o600 — POSIX에서 소유자 외 읽기 차단. Windows는 mode를 무시하지만 %TEMP%가 이미 사용자별이다.
 * 실패는 throw해 allocate가 매치를 버리게 한다(토큰 없이 뜬 DS는 결과를 보고할 수 없다).
 */
export function writeMatchConfig(matchId: string, cfg: MatchConfigFile): string
{
    const filePath = path.join(os.tmpdir(), `d1-match-${matchId}.json`);
    writeFileSync(filePath, JSON.stringify(cfg), { encoding: 'utf8', mode: 0o600 });
    writtenConfigPaths.add(filePath);

    return filePath;
}

/**
 * 안전망 삭제 — 정상 경로에서는 DS가 읽자마자 지운다. DS가 못 뜨거나 크래시한 경우를 대비해
 * 백엔드도 한 번 더 지운다. 준비 타임아웃(readyTimeoutMs)보다 넉넉히 뒤에 돌아 정상 부팅을 방해하지 않는다.
 */
export function scheduleConfigCleanup(filePath: string, matchId: string): void
{
    const timer = setTimeout(() =>
    {
        if (deleteMatchConfig(filePath))
        {
            logger.warn({ matchId, filePath }, 'DS가 매치 설정 파일을 지우지 않음 — 백엔드가 정리');
        }
    }, ds.readyTimeoutMs * 2);
    timer.unref();
}

/**
 * 이 프로세스가 쓴 매치 설정 파일을 남김없이 지운다.
 * scheduleConfigCleanup의 안전망 타이머는 unref라 이벤트 루프를 붙잡지 않는다 — 서버는 오래 살아
 * 제때 돌지만, DS를 띄우고 곧장 끝나는 스크립트는 그 전에 종료돼 토큰이 든 파일이 %TEMP%에 쌓인다.
 * DS를 spawn하는 하네스가 종료 직전에 부른다.
 */
export function deleteMatchConfigs(): void
{
    for (const filePath of [...writtenConfigPaths])
    {
        deleteMatchConfig(filePath);
    }
}

/** 설정 파일 하나를 지우고 추적에서 뺀다. 이미 없으면(DS가 읽고 삭제) 조용히 넘어간다. */
function deleteMatchConfig(filePath: string): boolean
{
    writtenConfigPaths.delete(filePath);

    try
    {
        if (!existsSync(filePath))
        {
            return false;
        }

        unlinkSync(filePath);

        return true;
    }
    catch (err)
    {
        logger.warn({ err, filePath }, '매치 설정 파일 정리 실패');

        return false;
    }
}
