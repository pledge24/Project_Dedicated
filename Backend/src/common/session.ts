// 단일 세션 강제 — users.token_version 현재값 조회 + 세션 대체 알림 버스.
// HTTP 미들웨어·매칭 WS 공용.
import type { RowDataPacket } from 'mysql2';
import { EventEmitter } from 'node:events';

import { queryOne } from './db.js';

interface TokenVersionRow extends RowDataPacket
{
    token_version: number;
}

/** userId의 현재 token_version. 유저가 없으면 null. */
export async function getCurrentTokenVersion(userId: number): Promise<number | null>
{
    const row = await queryOne<TokenVersionRow>(
        'SELECT token_version FROM users WHERE id = ? LIMIT 1',
        [userId]
    );

    return row ? row.token_version : null;
}

// 세션 대체 알림 — auth(발행)와 매칭 WS(구독)의 직접 의존을 끊는 인프로세스 버스.
const sessionEvents = new EventEmitter();

/** 새 로그인으로 userId의 이전 세션이 대체됐음을 알린다. */
export function emitSuperseded(userId: number): void
{
    sessionEvents.emit('superseded', userId);
}

/** 세션 대체 구독. 반환된 함수로 해제한다. */
export function onSuperseded(listener: (userId: number) => void): () => void
{
    sessionEvents.on('superseded', listener);

    return () => sessionEvents.off('superseded', listener);
}
