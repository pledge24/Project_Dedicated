// 인증 비즈니스 로직 (service 레이어)
const repo = require('./auth.repository');
const passwordUtil = require('../common/password');
const jwtUtil = require('../common/jwt');
const { AppError, Codes } = require('../common/errors');

const DEFAULT_SCORE = 1000;

/**
 * 회원가입.
 * @param {string} loginId
 * @param {string} password
 * @param {string} nickname
 * @returns {Promise<{userId:number, nickname:string, score:number, token:string}>}
 */
async function register(loginId, password, nickname)
{
    if (await repo.findByLoginId(loginId))
    {
        throw new AppError(Codes.DUPLICATE_LOGIN_ID, '이미 사용 중인 ID입니다.');
    }
    if (await repo.findByNickname(nickname))
    {
        throw new AppError(Codes.DUPLICATE_NICKNAME, '이미 사용 중인 닉네임입니다.');
    }

    const passwordHash = await passwordUtil.hash(password);
    const userId = await repo.insertUser({ loginId, passwordHash, nickname });
    const token = jwtUtil.sign({ userId, nickname });

    return { userId, nickname, score: DEFAULT_SCORE, token };
}

/**
 * 로그인.
 * @param {string} loginId
 * @param {string} password
 * @returns {Promise<{userId:number, nickname:string, score:number, token:string}>}
 */
async function login(loginId, password)
{
    const row = await repo.findByLoginId(loginId);
    // ID/PW 어느 쪽이 틀린지 노출하지 않음 (enumeration 방지)
    if (!row || !(await passwordUtil.verify(password, row.password_hash)))
    {
        throw new AppError(Codes.INVALID_CREDENTIALS, 'ID 또는 비밀번호가 일치하지 않습니다.');
    }

    const token = jwtUtil.sign({ userId: row.id, nickname: row.nickname });
    return { userId: row.id, nickname: row.nickname, score: row.score, token };
}

module.exports = { register, login };
