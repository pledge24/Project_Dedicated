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

            // C++ 패리티 — Allman 중괄호. 한 줄 블록도 펼침(allowSingleLine off).
            '@stylistic/brace-style': ['error', 'allman'],

            // 들여쓰기 4칸(switch case 1단). curly가 넣은 중괄호/본문을 정렬한다.
            '@stylistic/indent': ['error', 4, { SwitchCase: 1 }],

            // 줄 끝 공백 제거(중괄호 줄바꿈 후 남는 trailing space 정리).
            '@stylistic/no-trailing-spaces': 'error',

            // 제어문 본문 중괄호 항상(한 줄이어도 생략 금지). 위치는 brace-style이 담당.
            'curly': ['error', 'all'],

            // 빈 줄: return 위 1줄, 여러 줄 표현식문끼리 사이 1줄.
            '@stylistic/padding-line-between-statements': ['error',
                { blankLine: 'always', prev: '*', next: 'return' },
                { blankLine: 'always', prev: 'multiline-expression', next: 'multiline-expression' },
            ],
        },
    },

    // 정합성 규칙(타입 정보 필요) — src만. 지금까지는 사람이 주석으로 방어하던
    // floating promise/미사용 심볼을 도구가 잡는다. scripts는 수동 sim이라 스타일만 적용.
    {
        files: ['src/**/*.ts'],
        languageOptions:
        {
            parser: tseslint.parser,
            parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
        },
        plugins:
        {
            '@typescript-eslint': tseslint.plugin,
        },
        rules:
        {
            '@typescript-eslint/no-floating-promises': 'error',
            '@typescript-eslint/no-misused-promises': 'error',
            '@typescript-eslint/await-thenable': 'error',
            '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
        },
    },
);
