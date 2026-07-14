import mysql from 'mysql2/promise';
// db-wipe.sql(TRUNCATE 전용)을 실행하는 파괴적 데이터 삭제 스크립트.
// 4개 게임 테이블(users 포함)의 모든 행을 비우고 AUTO_INCREMENT를 1로 리셋한다.
// 스키마/schema_migrations는 유지 → 재마이그레이션 불필요. 봇 오염 청소용.
// 풀과 동일한 접속 옵션(dbConnectionOptions)을 공유한다.
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { config } from '../src/common/config.js';
import { dbConnectionOptions } from '../src/common/db.js';
import { logger } from '../src/common/logger.js';

// 계정까지 지우므로 production에서는 거부 (실수 방지 가드).
if (config.nodeEnv === 'production')
{
    logger.error('db:wipe는 production에서 실행할 수 없음 — NODE_ENV 확인');
    process.exit(1);
}

const here = path.dirname(fileURLToPath(import.meta.url));
const sqlPath = path.resolve(here, '../src/common/db-wipe.sql');
const sql = await readFile(sqlPath, 'utf8');

const conn = await mysql.createConnection({
    ...dbConnectionOptions,
    multipleStatements: true,
});

try
{
    await conn.query(sql);
    logger.info('게임 데이터 전량 삭제 완료 (users 포함) — 스키마 유지, ID 리셋');
}
finally
{
    await conn.end();
}
