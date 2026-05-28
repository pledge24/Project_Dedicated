// db.sql을 읽어 multipleStatements로 일괄 실행하는 초기화 스크립트.
// d1 데이터베이스는 사전에 존재한다고 가정한다 (DB 자체는 Workbench 등으로 미리 생성).
// 풀과 동일한 접속 옵션(dbConnectionOptions)을 공유한다.
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import mysql from 'mysql2/promise';

import { dbConnectionOptions } from '../src/common/db.js';

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
    console.log('[db:init] 스키마 적용 완료');
}
finally
{
    await conn.end();
}
