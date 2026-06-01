-- 파괴적 리셋 전용. 모든 테이블을 드롭한다 (dev 데이터 전부 폐기).
-- 스키마 생성은 migrations/로 이관됨 → `npm run db:init` 후 반드시 `npm run migrate` 실행.
-- 자식 → 부모 순서.
DROP TABLE IF EXISTS match_participants;
DROP TABLE IF EXISTS matches;
DROP TABLE IF EXISTS player_profiles;
DROP TABLE IF EXISTS users;
DROP TABLE IF EXISTS schema_migrations;
