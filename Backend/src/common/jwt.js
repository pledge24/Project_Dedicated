// JWT 발급/검증 래퍼
const jwt = require('jsonwebtoken');

function getSecret()
{
    const s = process.env.JWT_SECRET;
    if (!s || s.length < 16)
    {
        throw new Error('JWT_SECRET이 .env에 없거나 너무 짧음 (16자 이상 필요)');
    }
    return s;
}

function getTtl()
{
    return process.env.JWT_TTL || '24h';
}

/**
 * @param {{userId:number, nickname:string}} payload
 * @returns {string} JWT
 */
function sign(payload)
{
    return jwt.sign(payload, getSecret(), { expiresIn: getTtl(), algorithm: 'HS256' });
}

/**
 * @param {string} token
 * @returns {{userId:number, nickname:string, iat:number, exp:number}}
 * @throws {Error} 위변조/만료 시
 */
function verify(token)
{
    return jwt.verify(token, getSecret(), { algorithms: ['HS256'] });
}

module.exports = { sign, verify };
