-- 006: 탈주 즉시정산 멱등 원장. (client_match_id, user_id)로 매치별 1회 정산 보장.
-- /leaver(즉시)와 /result(최종)가 프로필 FOR UPDATE 잠금 하에서 이 표를 확인/기록 →
-- 동일 탈주자 점수를 정확히 한 번만 적용(경합 시 escape/중복 차단).
-- client_match_id로 키잉(FK 없음): /leaver는 matches 행이 생기기 전에 INSERT하므로 matches FK 불가.
CREATE TABLE IF NOT EXISTS match_leaver_settlements (
  client_match_id CHAR(36)        NOT NULL,
  user_id         BIGINT UNSIGNED NOT NULL,
  score_delta     INT             NOT NULL,
  score_after     INT             NOT NULL,
  settled_at      DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (client_match_id, user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
