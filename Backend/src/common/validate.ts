// 입력 형식 검증. 위반 시 AppError(VALIDATION_FAILED) throw.
import { AppError, Codes } from './errors.js';

// 소문자 시작, 영소문자/숫자/_ 4~16자. 입력은 normalizeLoginId로 소문자 정규화 후 검증.
const RX_LOGIN_ID = /^[a-z][a-z0-9_]{3,15}$/;
// 출력 가능 ASCII(공백 포함) 8~64자. 복잡도(영문/숫자 혼합) 강제 안 함.
const RX_PASSWORD = /^[\x20-\x7E]{8,64}$/;
// 완성형 한글(가-힣) + 영문 + 숫자, 2~12자.
const RX_NICKNAME = /^[가-힣A-Za-z0-9]{2,12}$/;

/**
 * loginId를 저장/검증 전에 소문자로 정규화. 문자열이 아니면 그대로 반환(검증에서 걸림).
 * register/login 양쪽 핸들러에서 호출해 대소문자 차이로 같은 계정이 갈리지 않게 한다.
 */
export function normalizeLoginId(v: unknown): unknown
{
    return typeof v === 'string' ? v.toLowerCase() : v;
}

export function assertLoginId(loginId: unknown): asserts loginId is string
{
    if (!RX_LOGIN_ID.test(asString(loginId)))
    {
        throw new AppError(Codes.VALIDATION_FAILED, 'ID는 소문자로 시작하는 영소문자/숫자/_ 조합 4~16자여야 합니다.');
    }
}

export function assertPassword(password: unknown): asserts password is string
{
    if (!RX_PASSWORD.test(asString(password)))
    {
        throw new AppError(Codes.VALIDATION_FAILED, '비밀번호는 공백 포함 출력 가능한 ASCII 8~64자여야 합니다.');
    }
}

export function assertNickname(nickname: unknown): asserts nickname is string
{
    const s = asString(nickname);
    if (s !== s.trim())
    {
        throw new AppError(Codes.VALIDATION_FAILED, '닉네임 앞뒤 공백은 허용되지 않습니다.');
    }
    if (!RX_NICKNAME.test(s))
    {
        throw new AppError(Codes.VALIDATION_FAILED, '닉네임은 한글/영문/숫자 2~12자여야 합니다.');
    }
}

/** 정수 + 범위 검사 타입 가드. max 생략 시 상한 없음. */
export function isInt(v: unknown, min: number, max?: number): v is number
{
    return typeof v === 'number' && Number.isInteger(v) && v >= min && (max === undefined || v <= max);
}

/** 비문자열 입력을 빈 문자열로 — 정규식 검증 진입용 헬퍼. */
function asString(v: unknown): string
{
    return typeof v === 'string' ? v : '';
}
