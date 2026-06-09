// 백엔드 코드 순서 자동 강제 (flat config, ESLint v9+).
// 의미상 안전한 auto-fix 규칙만 켠다 — import/named/클래스 정렬 + Allman 중괄호.
// 최상위 선언 순서(공개 먼저, 헬퍼 아래)는 TDZ 위험 때문에 자동화하지 않고
// .claude/rules/backend-code-order.md 의 수동 규칙으로 통제한다.
import stylistic from '@stylistic/eslint-plugin';
import perfectionist from 'eslint-plugin-perfectionist';
import tseslint from 'typescript-eslint';

export default tseslint.config(
    { ignores: ['node_modules/**', 'dist/**'] },
    {
        files: ['**/*.ts'],
        languageOptions:
        {
            parser: tseslint.parser,
            parserOptions: { sourceType: 'module', ecmaVersion: 'latest' },
        },
        plugins:
        {
            '@stylistic': stylistic,
            perfectionist,
        },
        rules:
        {
            // import 정렬: 사이드이펙트(자리 고정) → 서드파티(값+타입) → 로컬(값+타입).
            // sortSideEffects 기본 false → import 'dotenv/config' 는 절대 이동 안 함.
            'perfectionist/sort-imports': ['error', {
                type: 'natural',
                order: 'asc',
                newlinesBetween: 1,
                groups: [
                    'side-effect',
                    'side-effect-style',
                    ['value-builtin', 'value-external', 'type-import'],
                    [
                        'value-internal', 'type-internal',
                        'value-parent', 'type-parent',
                        'value-sibling', 'type-sibling',
                        'value-index', 'type-index',
                    ],
                    'unknown',
                ],
            }],

            // 중괄호 안 named import/export 알파벳 정렬.
            'perfectionist/sort-named-imports': ['error', { type: 'natural', order: 'asc' }],
            'perfectionist/sort-named-exports': ['error', { type: 'natural', order: 'asc' }],

            // 클래스 멤버: 필드 → 생성자 → getter/setter → public→private 메서드.
            'perfectionist/sort-classes': ['error', {
                type: 'natural',
                order: 'asc',
                groups: [
                    'index-signature',
                    'static-property',
                    'public-property',
                    'protected-property',
                    'private-property',
                    'property',
                    'constructor',
                    ['get-method', 'set-method'],
                    'public-method',
                    'protected-method',
                    'private-method',
                    'method',
                    'unknown',
                ],
            }],

            // C++ 패리티 — Allman 중괄호. 한 줄 블록(() => { x; })은 허용.
            '@stylistic/brace-style': ['error', 'allman', { allowSingleLine: true }],
        },
    },
);
