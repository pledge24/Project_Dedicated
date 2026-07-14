-- 게임 데이터 전량 삭제(계정 포함). 스키마는 유지, AUTO_INCREMENT는 1로 리셋.
-- TRUNCATE는 FK 참조에 막히므로 체크를 잠시 끈다. schema_migrations는 유지 → 재마이그 불필요.
-- 자식 → 부모 순서.
SET FOREIGN_KEY_CHECKS = 0;
TRUNCATE TABLE match_participants;
TRUNCATE TABLE matches;
TRUNCATE TABLE player_profiles;
TRUNCATE TABLE users;
SET FOREIGN_KEY_CHECKS = 1;
