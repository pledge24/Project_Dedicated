// 에러 코드 카탈로그 + AppError 클래스

export const Codes = Object.freeze({
    VALIDATION_FAILED:   { http: 400, code: 'VALIDATION_FAILED' },
    INVALID_CREDENTIALS: { http: 401, code: 'INVALID_CREDENTIALS' },
    AUTH_REQUIRED:       { http: 401, code: 'AUTH_REQUIRED' },
    TOKEN_EXPIRED:       { http: 401, code: 'TOKEN_EXPIRED' },
    INVALID_TOKEN:       { http: 401, code: 'INVALID_TOKEN' },
    NOT_FOUND:           { http: 404, code: 'NOT_FOUND' },
    DUPLICATE_LOGIN_ID:  { http: 409, code: 'DUPLICATE_LOGIN_ID' },
    DUPLICATE_NICKNAME:  { http: 409, code: 'DUPLICATE_NICKNAME' },
    RATE_LIMITED:        { http: 429, code: 'RATE_LIMITED' },
    INTERNAL_ERROR:      { http: 500, code: 'INTERNAL_ERROR' },
});

export class AppError extends Error
{
    /**
     * @param {{http:number, code:string}} kind
     * @param {string} userMessage 사용자에게 보일 한글 메시지
     */
    constructor(kind, userMessage)
    {
        super(userMessage);
        this.kind = kind;
    }
}
