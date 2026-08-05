-- 009: roster에 DS의 플레이 시작 시각을 기록.
-- 재입장(GET /api/match/current)은 정책상 "매치 시작 전"에만 허용해야 하는데, 백엔드에는 그걸
-- 가릴 값이 없었다 — started_at은 매치 성사 시각이라 시작 게이트 대기 구간을 구분하지 못한다.
-- 메모리가 아니라 이 표에 두는 이유는 007/008과 같다: 백엔드가 죽어도 확정된 DS는 살아남아
-- 경기를 끝내므로, 재시작 직후 "아직 안 시작한 매치"로 되돌아가 재입장 주소를 다시 내주면 안 된다.
-- 기존 행과 시작 전 매치는 NULL. 재입장 조회는 NULL이 아니면 대상에서 제외한다.
ALTER TABLE match_rosters
  ADD COLUMN play_started_at DATETIME NULL AFTER started_at;
