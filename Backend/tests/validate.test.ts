import { describe, expect, it } from 'vitest';

import { isInt, normalizeLoginId, validateLoginId, validateNickname, validatePassword } from '../src/common/validate.js';

describe('validateLoginId', () =>
{
    it('소문자 시작 + 영소문자/숫자/_ 4~16자 허용', () =>
    {
        expect(() => validateLoginId('abcd')).not.toThrow();
        expect(() => validateLoginId('a123_456789abcde')).not.toThrow(); // 16자
    });

    it('경계 밖·형식 위반 거부', () =>
    {
        expect(() => validateLoginId('abc')).toThrow();                   // 3자
        expect(() => validateLoginId('a123_456789abcdef')).toThrow();     // 17자
        expect(() => validateLoginId('1abc')).toThrow();                  // 숫자 시작
        expect(() => validateLoginId('Abcd')).toThrow();                  // 대문자
        expect(() => validateLoginId(undefined)).toThrow();               // 비문자열
    });

    it('normalizeLoginId — 문자열은 소문자화, 비문자열은 그대로', () =>
    {
        expect(normalizeLoginId('AbCd')).toBe('abcd');
        expect(normalizeLoginId(42)).toBe(42);
    });
});

describe('validatePassword', () =>
{
    it('출력 가능 ASCII 8~64자 허용(공백 포함)', () =>
    {
        expect(() => validatePassword('pass word1')).not.toThrow();
        expect(() => validatePassword('x'.repeat(64))).not.toThrow();
    });

    it('7자·65자·비ASCII 거부', () =>
    {
        expect(() => validatePassword('1234567')).toThrow();
        expect(() => validatePassword('x'.repeat(65))).toThrow();
        expect(() => validatePassword('비밀번호12345')).toThrow();
    });
});

describe('validateNickname', () =>
{
    it('한글/영문/숫자 2~12자 허용', () =>
    {
        expect(() => validateNickname('가나')).not.toThrow();
        expect(() => validateNickname('Player1')).not.toThrow();
        expect(() => validateNickname('가나다라마바사아자차카타')).not.toThrow(); // 12자
    });

    it('길이 밖·앞뒤 공백·미완성 한글 거부', () =>
    {
        expect(() => validateNickname('가')).toThrow();                    // 1자
        expect(() => validateNickname('가나다라마바사아자차카타파')).toThrow(); // 13자
        expect(() => validateNickname(' 가나')).toThrow();                 // 앞 공백
        expect(() => validateNickname('가나 ')).toThrow();                 // 뒤 공백
        expect(() => validateNickname('ㄱㄴ')).toThrow();                  // 자모만
    });
});

describe('isInt', () =>
{
    it('정수 + 범위 검사', () =>
    {
        expect(isInt(5, 0, 10)).toBe(true);
        expect(isInt(0, 0)).toBe(true);
        expect(isInt(-1, 0)).toBe(false);
        expect(isInt(11, 0, 10)).toBe(false);
        expect(isInt(1.5, 0, 10)).toBe(false);
        expect(isInt('5', 0, 10)).toBe(false);
    });
});
