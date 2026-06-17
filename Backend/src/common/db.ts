// MySQL2 풀 싱글톤
import type { ExecuteValues, RowDataPacket } from 'mysql2';
import mysql from 'mysql2/promise';
import type { Pool, PoolConnection } from 'mysql2/promise';

import { config } from './config.js';

// 풀과 1회용 connection(예: db:init)이 공유하는 접속 옵션.
// 풀 전용 옵션(connectionLimit 등)은 여기에 두지 않는다.
export const dbConnectionOptions = Object.freeze({
    host:     config.db.host,
    port:     config.db.port,
    user:     config.db.user,
    password: config.db.password,
    database: config.db.name,
});

let pool: Pool | null = null;

export function getPool(): Pool
{
    if (pool)
    {
        return pool;
    }

    pool = mysql.createPool({
        ...dbConnectionOptions,
        waitForConnections: true,
        connectionLimit: 10,
        queueLimit: 0,
        charset: 'utf8mb4_unicode_ci',
    });

    return pool;
}

/** SELECT … LIMIT 1 단건 조회. 행이 없으면 null. */
export async function queryOne<T extends RowDataPacket>(sql: string, params?: ExecuteValues[]): Promise<T | null>
{
    const [rows] = await getPool().execute<T[]>(sql, params);

    return rows.length ? rows[0] : null;
}

/** 트랜잭션 경계 헬퍼 — begin→fn→commit, 실패 시 rollback, 항상 release. */
export async function withTransaction<T>(fn: (conn: PoolConnection) => Promise<T>): Promise<T>
{
    const conn = await getPool().getConnection();
    try
    {
        await conn.beginTransaction();
        const result = await fn(conn);
        await conn.commit();

        return result;
    }
    catch (err)
    {
        await conn.rollback();
        throw err;
    }
    finally
    {
        conn.release();
    }
}

/** 레디니스 프로브용 — 풀에서 커넥션 받아 ping 후 반환. 실패 시 throw. */
export async function pingDb(): Promise<void>
{
    const conn = await getPool().getConnection();
    try
    {
        await conn.ping();
    }
    finally
    {
        conn.release();
    }
}

/** graceful shutdown용 — pool이 있으면 닫고 null로 리셋. */
export async function closePool(): Promise<void>
{
    if (!pool)
    {
        return;
    }
    const p = pool;
    pool = null;
    await p.end();
}
