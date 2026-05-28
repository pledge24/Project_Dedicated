-- D1 백엔드 v1 스키마 — users 테이블만.
-- d1 데이터베이스는 사전에 존재한다고 가정. db:init 스크립트가 .env의 DB_NAME으로 접속한다.
-- 적용: npm run db:init

-- 유저 테이블
CREATE TABLE IF NOT EXISTS users (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  login_id      VARCHAR(32)  NOT NULL,
  password_hash VARCHAR(72)  NOT NULL,            -- bcrypt 60자 + 여유
  nickname      VARCHAR(20)  NOT NULL,
  score         INT          NOT NULL DEFAULT 1000,
  created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY ux_users_login_id (login_id),
  UNIQUE KEY ux_users_nickname (nickname),
  KEY idx_users_score_desc (score DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
