// bcryptjs 래퍼 (Windows node-gyp 회피)
import bcrypt from 'bcryptjs';

const ROUNDS = 10;

/**
 * @param {string} plain
 * @returns {Promise<string>} bcrypt 해시
 */
export async function hash(plain)
{
    return bcrypt.hash(plain, ROUNDS);
}

/**
 * @param {string} plain
 * @param {string} hashed
 * @returns {Promise<boolean>}
 */
export async function verify(plain, hashed)
{
    return bcrypt.compare(plain, hashed);
}
