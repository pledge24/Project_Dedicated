-- 005: 탈주(다른 기기 로그인으로 게임중 kick된 플레이어) 표식.
-- DS가 결과 보고 시 abandoned=1로 올리면 백엔드가 최하위 + 추가 감점(leaverPenalty)을 적용한다.
ALTER TABLE match_participants
  ADD COLUMN abandoned TINYINT(1) NOT NULL DEFAULT 0;
