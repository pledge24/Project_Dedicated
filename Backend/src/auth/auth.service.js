// 인증 비즈니스 로직 (service 레이어)
import * as repo from './auth.repository.js';
import * as passwordUtil from '../common/password.js';
import * as jwtUtil from '../common/jwt.js';
import { AppError, Codes } from '../common/errors.js';

// 가입 직후 자동 로그인을 막기 위해 register는 토큰을 발급하지 않는다.
// 클라는 별도로 /api/auth/login을 호출해 세션을 시작한다.
/**
 * 회원가입.
 * @param {string} loginId
 * @param {string} password
 * @param {string} nickname
 * @returns {Promise<{userId:number, nickname:string, score:number, level:number, exp:number}>}
 */
export async function register(loginId, password, nickname)
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

    // 단일 진실: DB의 DEFAULT가 변하면 응답도 자동 반영되도록 SELECT.
    const profile = await repo.findProfileByUserId(userId);
    return {
        userId,
        nickname,
        score: profile.score,
        level: profile.level,
        exp:   profile.exp,
    };
}

/**
 * 로그인.
 * @param {string} loginId
 * @param {string} password
 * @returns {Promise<{userId:number, nickname:string, score:number, level:number, exp:number, token:string}>}
 */
export async function login(loginId, password)
{
    const row = await repo.findByLoginId(loginId);
    // ID/PW 어느 쪽이 틀린지 노출하지 않음 (enumeration 방지)
    if (!row || !(await passwordUtil.verify(password, row.password_hash)))
    {
        throw new AppError(Codes.INVALID_CREDENTIALS, 'ID 또는 비밀번호가 일치하지 않습니다.');
    }

    const profile = await repo.findProfileByUserId(row.id);
    const token = jwtUtil.sign({ userId: row.id, nickname: row.nickname });
    return {
        userId:   row.id,
        nickname: row.nickname,
        score:    profile.score,
        level:    profile.level,
        exp:      profile.exp,
        token,
    };
}
