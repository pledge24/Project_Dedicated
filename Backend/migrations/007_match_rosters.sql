-- 007: 매치 성사 시점의 명단(roster) 영속화.
-- 백엔드가 죽어도 DS는 살아남아 경기를 끝내므로(shutdownUncommitted), 재시작한 백엔드가
-- 그 DS의 serverToken을 검증할 수 있어야 한다. roster가 메모리뿐이면 결과 POST가 404 →
-- 경기 결과·ELO·경험치 영구 유실. 이 표가 그 단절을 메운다.
-- match_id로 키잉(FK 없음): roster는 매치 '시작' 시점, matches 행은 '종료' 시점에 생긴다.
-- (006_leaver_settlements와 동일한 이유.)
-- players_json: [{userId, nickname, joinToken, bot?, rating?}] — 좌석 수가 룰에 종속돼
-- 정규화 이득이 없고, 조회는 항상 매치 단위 전량이라 JSON 한 컬럼이 적합.
CREATE TABLE IF NOT EXISTS match_rosters (
  match_id     CHAR(36)     NOT NULL,
  server_token VARCHAR(64)  NOT NULL,
  map_name     VARCHAR(255) NOT NULL,
  started_at   DATETIME(3)  NOT NULL,
  players_json JSON         NOT NULL,
  PRIMARY KEY (match_id),
  INDEX idx_mr_started_at (started_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
