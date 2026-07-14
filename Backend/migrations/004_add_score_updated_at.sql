-- 004: add_score_updated_at
-- 랭킹 동점 타이브레이크용 '점수 갱신 시점' 컬럼. 점수가 실제로 바뀐 매치만 기록(±0은 유지).
-- ON UPDATE 없음 → exp/matches_played 등 다른 컬럼이 바뀌어도 자동 갱신되지 않는다(수동 관리).
ALTER TABLE player_profiles
  ADD COLUMN score_updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) AFTER updated_at;

-- 기존 행 백필: 마지막 매치 시각(없으면 행 갱신 시각)으로 근사.
UPDATE player_profiles SET score_updated_at = COALESCE(last_match_at, updated_at);

-- 정렬 인덱스를 (score DESC, score_updated_at ASC)로 확장 → 새 ORDER BY 커버(InnoDB가 PK user_id를 뒤에 붙임).
ALTER TABLE player_profiles DROP INDEX idx_pp_score_desc;
ALTER TABLE player_profiles ADD KEY idx_pp_score_desc (score DESC, score_updated_at ASC);
