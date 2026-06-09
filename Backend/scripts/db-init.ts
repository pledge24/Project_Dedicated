import mysql from 'mysql2/promise';
// db.sql(드롭 전용)을 실행하는 파괴적 리셋 스크립트.
// 모든 테이블을 폐기만 한다 — 스키마 재생성은 migrations/(npm run migrate)가 담당.
// d1 데이터베이스는 사전에 존재한다고 가정한다 (DB 자체는 Workbench 등으로 미리 생성).
// 풀과 동일한 접속 옵션(dbConnectionOptions)을 공유한다.
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { dbConnectionOptions } from '../src/common/db.js';
import { logger } from '../src/common/logger.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const sqlPath = path.resolve(here, '../src/common/db.sql');
const sql = await readFile(sqlPath, 'utf8');

const conn = await mysql.createConnection({
    ...dbConnectionOptions,
    multipleStatements: true,
});

try
{
    await conn.query(sql);
    logger.info('파괴적 리셋 완료 — 이어서 `npm run migrate` 실행 필요');
}
finally
{
    await conn.end();
}
