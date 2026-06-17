// 에러 코드 카탈로그 + AppError 클래스

export interface ErrorKind
{
    http: number;
    code: string;
}

export const Codes = {
    VALIDATION_FAILED:   { http: 400, code: 'VALIDATION_FAILED' },
    INVALID_CREDENTIALS: { http: 401, code: 'INVALID_CREDENTIALS' },
    AUTH_REQUIRED:       { http: 401, code: 'AUTH_REQUIRED' },
    TOKEN_EXPIRED:       { http: 401, code: 'TOKEN_EXPIRED' },
    INVALID_TOKEN:       { http: 401, code: 'INVALID_TOKEN' },
    NOT_FOUND:           { http: 404, code: 'NOT_FOUND' },
    MATCH_NOT_FOUND:     { http: 404, code: 'MATCH_NOT_FOUND' },
    DUPLICATE_LOGIN_ID:  { http: 409, code: 'DUPLICATE_LOGIN_ID' },
    DUPLICATE_NICKNAME:  { http: 409, code: 'DUPLICATE_NICKNAME' },
    ALREADY_IN_QUEUE:    { http: 409, code: 'ALREADY_IN_QUEUE' },
    RESULT_ALREADY_SUBMITTED: { http: 409, code: 'RESULT_ALREADY_SUBMITTED' },
    BAD_MESSAGE:         { http: 400, code: 'BAD_MESSAGE' },
    INVALID_RESULT:      { http: 400, code: 'INVALID_RESULT' },
    // 결과 보고는 DS만 — 서버 토큰 미첨부(401) / 토큰 불일치(403).
    SERVER_AUTH_REQUIRED: { http: 401, code: 'SERVER_AUTH_REQUIRED' },
    INVALID_SERVER_TOKEN: { http: 403, code: 'INVALID_SERVER_TOKEN' },
    RATE_LIMITED:        { http: 429, code: 'RATE_LIMITED' },
    INTERNAL_ERROR:      { http: 500, code: 'INTERNAL_ERROR' },
} as const satisfies Record<string, ErrorKind>;

export class AppError extends Error
{
    kind: ErrorKind;

    constructor(kind: ErrorKind, userMessage: string)
    {
        super(userMessage);
        this.name = 'AppError';
        this.kind = kind;
    }
}
