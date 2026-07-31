import { describe, expect, it } from 'vitest';

import { sign, verify } from '../src/common/jwt.js';

describe('jwt sign/verify', () =>
{
    it('발급한 토큰의 클레임이 그대로 복원된다', () =>
    {
        const token = sign({ userId: 42, nickname: '테스터', tokenVersion: 3 });
        const decoded = verify(token);

        expect(decoded.userId).toBe(42);
        expect(decoded.nickname).toBe('테스터');
        expect(decoded.tokenVersion).toBe(3);
        expect(decoded.exp).toBeGreaterThan(decoded.iat);
    });

    it('서명 훼손·임의 문자열은 throw', () =>
    {
        const token = sign({ userId: 1, nickname: 'a', tokenVersion: 1 });
        const tampered = `${token.slice(0, -2)}xx`;

        expect(() => verify(tampered)).toThrow();
        expect(() => verify('not-a-jwt')).toThrow();
    });
});
