-- 008: roster에 DS 주소를 함께 보관.
-- 재입장(GET /api/match/current)은 "이 유저가 지금 어느 DS로 가야 하는가"를 답해야 하는데,
-- 007의 roster에는 명단만 있고 주소가 없었다(주소는 ws.ts 지역 변수로만 존재 → 푸시 후 소멸).
-- 백엔드가 재시작해도 살아남은 DS의 주소를 답할 수 있어야 하므로 메모리가 아니라 이 표에 둔다.
-- 기존 행은 주소를 모른다 → NULL 허용. 재입장 조회는 NULL이면 대상에서 제외한다.
ALTER TABLE match_rosters
  ADD COLUMN server_host VARCHAR(255)    NULL AFTER server_token,
  ADD COLUMN server_port SMALLINT UNSIGNED NULL AFTER server_host;
