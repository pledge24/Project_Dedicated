// bcryptjs 래퍼 (Windows node-gyp 회피)
import bcrypt from 'bcryptjs';

const ROUNDS = 10;

/** 평문 → bcrypt 해시. */
export async function hash(plain: string): Promise<string>
{
    return bcrypt.hash(plain, ROUNDS);
}

/** 평문이 해시와 일치하는지. */
export async function verify(plain: string, hashed: string): Promise<boolean>
{
    return bcrypt.compare(plain, hashed);
}
