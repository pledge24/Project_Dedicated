// JWT 발급/검증 래퍼
import jwt from 'jsonwebtoken';

import { config } from './config.js';

/**
 * @param {{userId:number, nickname:string}} payload
 * @returns {string} JWT
 */
export function sign(payload)
{
    return jwt.sign(payload, config.jwt.secret, { expiresIn: config.jwt.ttl, algorithm: 'HS256' });
}

/**
 * @param {string} token
 * @returns {{userId:number, nickname:string, iat:number, exp:number}}
 * @throws {Error} 위변조/만료 시
 */
export function verify(token)
{
    return jwt.verify(token, config.jwt.secret, { algorithms: ['HS256'] });
}
