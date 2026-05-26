// MySQL2 풀 싱글톤
const mysql = require('mysql2/promise');

/** @type {import('mysql2/promise').Pool|null} */
let pool = null;

function getPool()
{
    if (pool) return pool;

    pool = mysql.createPool({
        host: process.env.DB_HOST || '127.0.0.1',
        port: Number(process.env.DB_PORT) || 3306,
        user: process.env.DB_USER || 'root',
        password: process.env.DB_PASS || '',
        database: process.env.DB_NAME || 'd1',
        waitForConnections: true,
        connectionLimit: 10,
        queueLimit: 0,
        charset: 'utf8mb4_unicode_ci',
    });
    return pool;
}

module.exports = { getPool };
