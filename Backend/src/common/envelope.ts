// 응답 봉투 헬퍼
import type { BackendResponse } from './types.js';

/** 성공 응답 만들기. */
export function ok<T>(data: T): BackendResponse<T>
{
    return { ok: true, data };
}

/** 실패 응답 만들기. */
export function fail(code: string, message: string): BackendResponse
{
    return { ok: false, error: { code, message } };
}
