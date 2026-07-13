-- 003: backfill_level
-- 기존 누적 exp에 맞춰 level 재계산 (전 구간 1000 exp당 1레벨). 멱등 — 재실행해도 동일.
UPDATE player_profiles SET level = FLOOR(exp / 1000) + 1;
