// 환경 변수 로드 + 부팅 시 검증 + 캐싱. 모든 env 접근은 이 모듈을 통한다.
// 잘못된 .env로 서버가 "떠 있다고 착각하는" 상태를 막기 위해 부팅 시점에 fail-fast.
import 'dotenv/config';

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
        resultMax: asNumber('RATE_LIMIT_RESULT_MAX', 30),    // DS 결과 보고(/api/match/result)
        pollMax:   asNumber('RATE_LIMIT_POLL_MAX', 300),     // DS kick 폴링(/api/match/:id/kicks) — 매치당 12/분 × 다수 매치가 같은 host IP
        rankingMax: asNumber('RATE_LIMIT_RANKING_MAX', 60),  // 랭킹 조회(/api/ranking)
        sessionMax: asNumber('RATE_LIMIT_SESSION_MAX', 60),  // 세션 조회(/me·/heartbeat) — requireAuth가 매번 DB SELECT라 무제한이면 증폭됨
        wsMax: asNumber('RATE_LIMIT_WS_MAX', 100),           // 매칭 WS 메시지(연결당) — OWASP 시작점
    }),
    match: Object.freeze({
        playersPerMatch: asNumber('MATCH_PLAYERS_PER_MATCH', 4),
        baseWindow:      asNumber('MATCH_BASE_WINDOW', 200),   // 시작 점수 윈도우(±)
        expandRate:      asNumber('MATCH_EXPAND_RATE', 50),    // 대기 1초당 윈도우 확장폭
        maxWindow:       asNumber('MATCH_MAX_WINDOW', 2000),   // 윈도우 상한
        cycleMs:         asNumber('MATCH_CYCLE_MS', 1000),     // 매칭 사이클 주기
        heartbeatMs:     asNumber('MATCH_HEARTBEAT_MS', 30_000),
        eloK:            asNumber('MATCH_ELO_K', 32),          // ELO K-factor
        scoreFloor:      asNumber('MATCH_SCORE_FLOOR', 100),   // 점수 하한
        scoreCeiling:    asNumber('MATCH_SCORE_CEILING', 5000), // 점수 상한
        leaverPenalty:   asNumber('MATCH_LEAVER_PENALTY', 30), // 탈주 시 최하위 배점에 더해질 추가 감점(양수=감점폭)
        // 봇전(Bot-Fill): 이 시간 넘게 매치가 안 잡힌 유저를 봇 3명과 즉시 게임에 투입. 봇 점수는 플레이어 ± spread.
        botFill: Object.freeze({
            enabled:      process.env.MATCH_BOT_FILL_ENABLED !== 'false', // 기본 on, MATCH_BOT_FILL_ENABLED=false로 차단
            waitMs:       asNumber('MATCH_BOT_FILL_MS', 30_000),  // 대기 임계(넘으면 봇전)
            ratingSpread: asNumber('MATCH_BOT_RATING_SPREAD', 100), // 봇 점수 = 플레이어 점수 ± 이 폭(랜덤)
        }),
        // ds.enabled=false면 아래 stub 주소 사용(봇/알고리즘 테스트 경로 보존).
        stubServer: Object.freeze({
            host: process.env.MATCH_STUB_HOST || '127.0.0.1',
            port: asNumber('MATCH_STUB_PORT', 7777),
        }),
        // 실제 Dedicated Server 할당(매치당 spawn). enabled=true일 때만 D1Server.exe를 띄운다.
        ds: Object.freeze({
            enabled:       process.env.MATCH_DS_ENABLED === 'true',
            exePath:       process.env.MATCH_DS_EXE || '',  // 머신별 절대경로 — DS 사용 시 .env에서 지정
            map:           process.env.MATCH_DS_MAP || '/Game/D1/Maps/MP_Ingame', // 쿡되지 않은 빌드에선 /Game/Maps/MP_Test로 임시 교체
            host:          process.env.MATCH_DS_HOST || '127.0.0.1',
            portMin:       asNumber('MATCH_DS_PORT_MIN', 7777),
            portMax:       asNumber('MATCH_DS_PORT_MAX', 7787),
            readyTimeoutMs: asNumber('MATCH_DS_READY_TIMEOUT_MS', 30_000),  // DS가 준비 콜백(POST /ready)을 보낼 상한. 최악 콜드부팅보다 넉넉해야 함
            maxLifetimeMs: asNumber('MATCH_DS_MAX_LIFETIME_MS', 900_000), // 15분 후 강제 회수
        }),
    }),
    // 랭킹 조회 페이지네이션 한도. limit 미지정 시 defaultLimit, 상한은 maxLimit로 clamp.
    ranking: Object.freeze({
        defaultLimit: asNumber('RANKING_DEFAULT_LIMIT', 50),
        maxLimit:     asNumber('RANKING_MAX_LIMIT', 100),
        // offset 상한 — MySQL은 OFFSET N을 N행 스캔 후 버리므로 큰 값이 그대로 인덱스 풀스캔이 된다.
        maxOffset:    asNumber('RANKING_MAX_OFFSET', 10_000),
    }),
});

// DS 사용 시 실행 파일 경로 필수 (머신별이라 소스에 기본값을 두지 않음)
if (config.match.ds.enabled && !config.match.ds.exePath)
{
    fail('MATCH_DS_ENABLED=true면 MATCH_DS_EXE(D1Server.exe 절대경로)가 필요함 — .env에 설정');
}

function fail(reason: string): never
{
    throw new Error(`[Config] ${reason}`);
}

function required(name: string, extraCheck?: (v: string, name: string) => void): string
{
    const v = process.env[name];
    if (!v)
    {
        fail(`env ${name}이(가) 비어있음`);
    }
    if (extraCheck)
    {
        extraCheck(v, name);
    }

    return v;
}

function asNumber(name: string, def: number): number
{
    const raw = process.env[name];
    if (raw === undefined || raw === '')
    {
        return def;
    }
    const n = Number(raw);
    if (!Number.isFinite(n))
    {
        fail(`env ${name}은 숫자여야 함 (값='${raw}')`);
    }

    return n;
}

function validateJwtSecret(v: string, name: string): void
{
    if (v.length < 16)
    {
        fail(`${name}은 16자 이상이어야 함 (현재 ${v.length}자)`);
    }
    if (v.includes('please-change'))
    {
        fail(`${name}이 .env.example 디폴트 문자열 그대로임 — 임의 값으로 교체 필요`);
    }
}
