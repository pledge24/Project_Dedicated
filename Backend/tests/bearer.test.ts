import { describe, expect, it } from 'vitest';

import { extractBearerToken } from '../src/common/bearer.js';

describe('extractBearerToken', () =>
{
    it('정상 형식에서 토큰만 추출(주변 공백 trim)', () =>
    {
        expect(extractBearerToken('Bearer abc.def')).toBe('abc.def');
        expect(extractBearerToken('Bearer  spaced ')).toBe('spaced');
    });

    it('헤더 없음·접두사 불일치·빈 토큰은 null', () =>
    {
        expect(extractBearerToken(undefined)).toBeNull();
        expect(extractBearerToken('')).toBeNull();
        expect(extractBearerToken('Basic abc')).toBeNull();
        expect(extractBearerToken('bearer abc')).toBeNull();  // 대소문자 구분
        expect(extractBearerToken('Bearerabc')).toBeNull();   // 공백 없음
        expect(extractBearerToken('Bearer ')).toBeNull();     // 토큰 없음
        expect(extractBearerToken('Bearer    ')).toBeNull();  // 공백만
    });
});
