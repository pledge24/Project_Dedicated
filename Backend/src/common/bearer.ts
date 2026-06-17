// Authorization: Bearer <token> 추출. 프레임워크 중립(Express·ws 공용).
export const BEARER_PREFIX = 'Bearer ';

/** 'Bearer <token>'에서 토큰만 반환. 형식 위반·빈 토큰이면 null. */
export function extractBearerToken(authorization: string | undefined): string | null
{
    if (!authorization || !authorization.startsWith(BEARER_PREFIX))
    {
        return null;
    }

    const token = authorization.slice(BEARER_PREFIX.length).trim();

    return token || null;
}
