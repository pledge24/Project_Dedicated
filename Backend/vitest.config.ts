// 단위 테스트 러너 설정 — DB 불필요한 순수 모듈만 대상(tests/*.test.ts).
// DB 의존 통합 검증은 scripts/*-sim.ts 수동 실행 경로를 유지한다.
import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        include: ['tests/**/*.test.ts'],
        setupFiles: ['tests/setup.ts'],
    },
});
