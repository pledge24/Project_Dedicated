-- 001: 초기 스키마 (인증 + 게임 메타 + 매치 기록).
-- 베이스라인 안전: CREATE TABLE IF NOT EXISTS — 이미 db:init된 DB 위에 적용해도 no-op.

-- 인증 전용
CREATE TABLE IF NOT EXISTS users (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  login_id      VARCHAR(32)  NOT NULL,
  password_hash VARCHAR(72)  NOT NULL,            -- bcrypt 60자 + 여유
  nickname      VARCHAR(20)  NOT NULL,
  created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY ux_users_login_id (login_id),
  UNIQUE KEY ux_users_nickname (nickname)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 1:1 게임 메타데이터 (랭크/레벨/EXP/승패/최근 매치)
CREATE TABLE IF NOT EXISTS player_profiles (
  user_id        BIGINT UNSIGNED NOT NULL,
  score          INT             NOT NULL DEFAULT 1000,   -- 랭크 포인트
  level          SMALLINT UNSIGNED NOT NULL DEFAULT 1,    -- 비정규화: 로비 핫패스
  exp            INT UNSIGNED    NOT NULL DEFAULT 0,      -- 전체 누적
  wins           INT UNSIGNED    NOT NULL DEFAULT 0,
  losses         INT UNSIGNED    NOT NULL DEFAULT 0,
  matches_played INT UNSIGNED    NOT NULL DEFAULT 0,
  last_match_at  DATETIME(3)     NULL,                    -- 비활성/휴면 식별
  updated_at     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (user_id),
  CONSTRAINT fk_pp_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
  KEY idx_pp_score_desc (score DESC),
  KEY idx_pp_level_desc (level DESC, exp DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 매치 1행
CREATE TABLE IF NOT EXISTS matches (
  id               BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  client_match_id  CHAR(36)     NOT NULL,                                    -- DS가 매치 시작 시 생성한 UUID, 재시도/중복 송신 방지
  map_name         VARCHAR(64)  NOT NULL,
  started_at       DATETIME(3)  NOT NULL,                                    -- UTC
  ended_at         DATETIME(3)  NOT NULL,                                    -- UTC
  duration_sec     INT UNSIGNED NOT NULL,
  end_reason       ENUM('winner','draw','time_expired','abort') NOT NULL,
  winner_user_id   BIGINT UNSIGNED NULL,                                     -- end_reason!='winner'이면 NULL
  created_at       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY ux_matches_client_id (client_match_id),
  CONSTRAINT fk_matches_winner FOREIGN KEY (winner_user_id) REFERENCES users(id) ON DELETE SET NULL,
  KEY idx_matches_ended_at_desc (ended_at DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 매치 × 참가자 (1매치 = 4 rows)
CREATE TABLE IF NOT EXISTS match_participants (
  id                 BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id           BIGINT UNSIGNED NOT NULL,
  user_id            BIGINT UNSIGNED NOT NULL,
  nickname_snapshot  VARCHAR(20)     NOT NULL,       -- 닉변/삭제 대비 감사 스냅샷
  slot_index         TINYINT UNSIGNED NOT NULL,       -- 0~3
  placement          TINYINT UNSIGNED NOT NULL,       -- 1~4 (동시 사망 동일 placement 허용 — 게임 룰)
  lives_left         TINYINT UNSIGNED NOT NULL,       -- 0~3
  exp_gained         INT  NOT NULL DEFAULT 0,
  score_delta        INT  NOT NULL DEFAULT 0,         -- 랭크 점수 변화량
  created_at         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY ux_mp_match_user (match_id, user_id),
  UNIQUE KEY ux_mp_match_slot (match_id, slot_index),
  CONSTRAINT fk_mp_match FOREIGN KEY (match_id) REFERENCES matches(id) ON DELETE CASCADE,
  CONSTRAINT fk_mp_user  FOREIGN KEY (user_id)  REFERENCES users(id)   ON DELETE RESTRICT,
  KEY idx_mp_user_created (user_id, created_at DESC),
  KEY idx_mp_match_placement (match_id, placement)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
