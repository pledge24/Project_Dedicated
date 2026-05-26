// bcryptjs 래퍼 (Windows node-gyp 회피)
const bcrypt = require('bcryptjs');

const ROUNDS = 10;

/**
 * @param {string} plain
 * @returns {Promise<string>} bcrypt 해시
 */
async function hash(plain)
{
    return bcrypt.hash(plain, ROUNDS);
}

/**
 * @param {string} plain
 * @param {string} hashed
 * @returns {Promise<boolean>}
 */
async function verify(plain, hashed)
{
    return bcrypt.compare(plain, hashed);
}

module.exports = { hash, verify };
