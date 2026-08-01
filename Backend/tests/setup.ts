// 테스트 공통 env — config.ts의 fail-fast(required env)가 .env 없는 환경에서도 통과하도록 최소값 주입.
// 이미 설정돼 있으면(.env 로드 등) 건드리지 않는다.
process.env.DB_PASS ??= 'test-only-password';
process.env.JWT_SECRET ??= 'test-secret-0123456789abcdef';
