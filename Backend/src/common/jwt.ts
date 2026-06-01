// JWT 발급/검증 래퍼
import jwt from 'jsonwebtoken';
import type { SignOptions } from 'jsonwebtoken';

import { config } from './config.js';

export interface TokenPayload
{
    userId: number;
    nickname: string;
}

export interface DecodedToken extends TokenPayload
{
    iat: number;
    exp: number;
}

/** payload로 JWT 발급. */
export function sign(payload: TokenPayload): string
{
    return jwt.sign(payload, config.jwt.secret, {
        // ttl 은 '24h' 같은 문자열. SignOptions 의 expiresIn 리터럴 타입으로 좁힌다.
        expiresIn: config.jwt.ttl as SignOptions['expiresIn'],
        algorithm: 'HS256',
    });
}

/** 토큰 검증 후 클레임 반환. 위변조/만료 시 throw. */
export function verify(token: string): DecodedToken
{
    // verify 반환은 string|JwtPayload|Jwt 유니온. 우리 토큰은 항상 객체 payload 다.
    return jwt.verify(token, config.jwt.secret, { algorithms: ['HS256'] }) as DecodedToken;
}
