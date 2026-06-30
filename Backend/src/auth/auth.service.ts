import { AppError, Codes } from '../common/errors.js';
import * as jwtUtil from '../common/jwt.js';
import * as passwordUtil from '../common/password.js';
import type { AuthUserDTO, RegisterResultDTO } from '../common/types.js';
import * as repo from './auth.repository.js';

// 가입 직후 자동 로그인을 막기 위해 register는 토큰을 발급하지 않는다.
// 클라는 별도로 /api/auth/login을 호출해 세션을 시작한다.
/** 회원가입. */
export async function register(loginId: string, password: string, nickname: string): Promise<RegisterResultDTO>
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
    if (!profile)
    {
        throw new AppError(Codes.INTERNAL_ERROR, '가입 직후 프로필 조회에 실패했습니다.');
    }

    return {
        userId,
        nickname,
        score: profile.score,
        level: profile.level,
        exp:   profile.exp,
    };
}

/** 로그인. */
export async function login(loginId: string, password: string): Promise<AuthUserDTO>
{
    const row = await repo.findByLoginId(loginId);
    // ID/PW 어느 쪽이 틀린지 노출하지 않음 (enumeration 방지)
    if (!row || !(await passwordUtil.verify(password, row.password_hash)))
    {
        throw new AppError(Codes.INVALID_CREDENTIALS, 'ID 또는 비밀번호가 일치하지 않습니다.');
    }

    const profile = await repo.findProfileByUserId(row.id);
    if (!profile)
    {
        throw new AppError(Codes.INTERNAL_ERROR, '프로필 조회에 실패했습니다.');
    }
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

/** 현재 사용자 프로필 조회. 토큰 검증(requireAuth) 통과 후 호출된다. */
export async function getMe(userId: number, nickname: string): Promise<RegisterResultDTO>
{
    const profile = await repo.findProfileByUserId(userId);
    // 토큰은 유효하지만 계정이 사라진 경우 (삭제 등)
    if (!profile)
    {
        throw new AppError(Codes.NOT_FOUND, '사용자 정보를 찾을 수 없습니다.');
    }

    return {
        userId,
        nickname,
        score: profile.score,
        level: profile.level,
        exp:   profile.exp,
    };
}
