import type { RowDataPacket } from 'mysql2';
import mysql from 'mysql2/promise';
// 경량 마이그레이션 러너. schema_migrations로 적용 버전 추적, up-only.
// 명령: up(기본) | status | make <name>. db-init과 동일하게 dbConnectionOptions 공유.
import { readdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { dbConnectionOptions } from '../src/common/db.js';
import { logger } from '../src/common/logger.js';

interface MigrationRow extends RowDataPacket
{
    version: string;
}

const here = path.dirname(fileURLToPath(import.meta.url));
const migrationsDir = path.resolve(here, '../migrations');

const TRACKING_DDL = `
CREATE TABLE IF NOT EXISTS schema_migrations (
  version    VARCHAR(64)  NOT NULL,
  name       VARCHAR(255) NOT NULL,
  applied_at DATETIME(3)  NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;`;

/** 파일명 `NNN_name.sql`에서 version/name 추출. */
function parse(file: string): { version: string; name: string }
{
    const m = /^(\d+)_(.+)\.sql$/.exec(file);
    if (!m) throw new Error(`마이그레이션 파일명 규칙 위반: ${file} (예: 002_add_foo.sql)`);
    return { version: m[1], name: m[2] };
}

/** migrations/*.sql 정렬 목록. */
async function listFiles(): Promise<string[]>
{
    const all = await readdir(migrationsDir);
    return all.filter((f) => f.endsWith('.sql')).sort();
}

/** 추적 테이블 보장 + 적용된 version 집합 반환. */
async function appliedVersions(conn: mysql.Connection): Promise<Set<string>>
{
    await conn.query(TRACKING_DDL);
    const [rows] = await conn.query<MigrationRow[]>('SELECT version FROM schema_migrations');
    return new Set(rows.map((r) => r.version));
}

/** 미적용 마이그레이션을 순서대로 적용. */
async function up(): Promise<void>
{
    const conn = await mysql.createConnection({ ...dbConnectionOptions, multipleStatements: true });
    try
    {
        const done = await appliedVersions(conn);
        const pending = (await listFiles()).filter((f) => !done.has(parse(f).version));
        if (pending.length === 0)
        {
            logger.info('적용할 마이그레이션 없음');
            return;
        }
        for (const file of pending)
        {
            const { version, name } = parse(file);
            const sql = await readFile(path.join(migrationsDir, file), 'utf8');
            // 참고: MySQL은 DDL을 암묵 커밋하므로 다중 DDL 파일은 원자적이지 않다.
            // INSERT는 파일 전체 성공 후에만 → 실패 파일은 미기록·재시도(파일을 idempotent하게 작성).
            await conn.beginTransaction();
            try
            {
                await conn.query(sql);
                await conn.query('INSERT INTO schema_migrations (version, name) VALUES (?, ?)', [version, name]);
                await conn.commit();
                logger.info({ version, migration: name }, '마이그레이션 적용 완료');
            }
            catch (err)
            {
                await conn.rollback();
                logger.error({ err, file }, '마이그레이션 실패 — 롤백');
                throw err;
            }
        }
    }
    finally
    {
        await conn.end();
    }
}

/** 적용/미적용 현황 출력. */
async function status(): Promise<void>
{
    const conn = await mysql.createConnection({ ...dbConnectionOptions });
    try
    {
        const done = await appliedVersions(conn);
        const files = await listFiles();
        for (const file of files)
        {
            const { version } = parse(file);
            logger.info(`${done.has(version) ? '[적용됨]' : '[대기  ]'} ${file}`);
        }
        const orphan = [...done].filter((v) => !files.some((f) => parse(f).version === v));
        if (orphan.length > 0) logger.warn({ orphan }, '파일 없는 기록된 버전 존재');
    }
    finally
    {
        await conn.end();
    }
}

/** 다음 번호로 빈 마이그레이션 파일 생성. */
async function make(name: string): Promise<void>
{
    if (!/^[a-z0-9_]+$/.test(name)) throw new Error(`name은 [a-z0-9_]만 허용: ${name}`);
    const files = await listFiles();
    const maxVer = files.reduce((mx, f) => Math.max(mx, Number(parse(f).version)), 0);
    const version = String(maxVer + 1).padStart(3, '0');
    const file = `${version}_${name}.sql`;
    await writeFile(path.join(migrationsDir, file), `-- ${version}: ${name}\n`, { flag: 'wx' });
    logger.info({ file }, '마이그레이션 파일 생성');
}

const cmd = process.argv[2] ?? 'up';
try
{
    if (cmd === 'up') await up();
    else if (cmd === 'status') await status();
    else if (cmd === 'make')
    {
        const name = process.argv[3];
        if (!name) throw new Error('사용법: npm run migrate:make <name>');
        await make(name);
    }
    else
    {
        logger.error({ cmd }, '알 수 없는 명령 (up|status|make)');
        process.exit(1);
    }
}
catch (err)
{
    logger.error({ err }, '마이그레이션 명령 실패');
    process.exit(1);
}
