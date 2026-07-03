-- 002: 단일 세션 강제용 토큰 버전 컬럼.
-- 로그인마다 +1, JWT 클레임에 실어 발급 → 옛 토큰은 대조에서 거절(최신 로그인 우선).
ALTER TABLE users
  ADD COLUMN token_version INT UNSIGNED NOT NULL DEFAULT 0;
