// 응답 봉투 헬퍼
/** @typedef {import('./types.js').BackendResponse} BackendResponse */

/**
 * 성공 응답 만들기.
 * @param {*} data
 * @returns {BackendResponse}
 */
export function ok(data)
{
    return { ok: true, data };
}

/**
 * 실패 응답 만들기.
 * @param {string} code
 * @param {string} message
 * @returns {BackendResponse}
 */
export function fail(code, message)
{
    return { ok: false, error: { code, message } };
}
