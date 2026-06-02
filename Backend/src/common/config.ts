// 환경 변수 로드 + 부팅 시 검증 + 캐싱. 모든 env 접근은 이 모듈을 통한다.
// 잘못된 .env로 서버가 "떠 있다고 착각하는" 상태를 막기 위해 부팅 시점에 fail-fast.
import 'dotenv/config';

function fail(reason: string): never
{
    throw new Error(`[Config] ${reason}`);
}

function required(name: string, extraCheck?: (v: string, name: string) => void): string
{
    const v = process.env[name];
    if (!v) fail(`env ${name}이(가) 비어있음`);
    if (extraCheck) extraCheck(v, name);
    return v;
}

function asNumber(name: string, def: number): number
{
    const raw = process.env[name];
    if (raw === undefined || raw === '') return def;
    const n = Number(raw);
    if (!Number.isFinite(n)) fail(`env ${name}은 숫자여야 함 (값='${raw}')`);
    return n;
}

function validateJwtSecret(v: string, name: string): void
{
    if (v.length < 16) fail(`${name}은 16자 이상이어야 함 (현재 ${v.length}자)`);
    if (v.includes('please-change')) fail(`${name}이 .env.example 디폴트 문자열 그대로임 — 임의 값으로 교체 필요`);
}

export const config = Object.freeze({
    nodeEnv: process.env.NODE_ENV || 'development',
    logLevel: process.env.LOG_LEVEL || 'info',
    port: asNumber('PORT', 3000),
    db: Object.freeze({
        host: process.env.DB_HOST || '127.0.0.1',
        port: asNumber('DB_PORT', 3306),
        user: process.env.DB_USER || 'root',
        password: required('DB_PASS'),
        name: process.env.DB_NAME || 'd1',
    }),
    jwt: Object.freeze({
        secret: required('JWT_SECRET', validateJwtSecret),
        ttl: process.env.JWT_TTL || '24h',
    }),
    rateLimit: Object.freeze({
        windowMs: asNumber('RATE_LIMIT_WINDOW_MS', 60_000),
        loginMax: asNumber('RATE_LIMIT_LOGIN_MAX', 5),
        registerMax: asNumber('RATE_LIMIT_REGISTER_MAX', 10),
    }),
    // 매칭 큐 파라미터. score 디폴트 1000 기준 합리값. 전부 MATCH_* env로 override 가능.
    match: Object.freeze({
        playersPerMatch: asNumber('MATCH_PLAYERS_PER_MATCH', 4),
        baseWindow:      asNumber('MATCH_BASE_WINDOW', 200),   // 시작 점수 윈도우(±)
        expandRate:      asNumber('MATCH_EXPAND_RATE', 50),    // 대기 1초당 윈도우 확장폭
        maxWindow:       asNumber('MATCH_MAX_WINDOW', 2000),   // 윈도우 상한
        cycleMs:         asNumber('MATCH_CYCLE_MS', 1000),     // 매칭 사이클 주기
        heartbeatMs:     asNumber('MATCH_HEARTBEAT_MS', 30_000),
        // 매칭 성사 시 푸시할 서버 주소. 이번 슬라이스는 stub — 실제 DS 할당은 다음 슬라이스.
        stubServer: Object.freeze({
            host: process.env.MATCH_STUB_HOST || '127.0.0.1',
            port: asNumber('MATCH_STUB_PORT', 7777),
        }),
    }),
});
