-- D1 백엔드 v1 스키마
-- 적용: npm run db:init  (root 비밀번호 입력)

CREATE DATABASE IF NOT EXISTS d1
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE d1;

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
