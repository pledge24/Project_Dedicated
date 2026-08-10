# D1 Backend

봄버맨 PvP 백엔드 (Node.js + Express + ws + MySQL). **인증 · 매칭(WebSocket) · 매치 결과 · 랭킹** 슬라이스 포함 (Dedicated Server 연동 포함).

## 빠른 시작

1. **환경 변수** — `.env.example` 을 `.env` 로 복사한 뒤 값을 채운다 (모든 설정 변수가 문서화돼 있음).
2. **MySQL 비밀번호 설정** — `.env` 의 `DB_PASS=` 에 로컬 MySQL `root` 비밀번호를 적는다.
3. **JWT 시크릿 교체** — `JWT_SECRET=` 을 32바이트 이상의 임의 문자열로 바꾼다.
4. **DB 스키마 적용**:
   - 사전: `d1` 데이터베이스를 미리 만들어 둔다.
     ```sql
     CREATE DATABASE d1 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
     ```
   - `npm run migrate` — `migrations/`의 미적용 SQL을 순서대로 적용하고 `schema_migrations`에 기록한다.
     기존 DB 위에 돌려도 안전(CREATE IF NOT EXISTS, 데이터 보존).
   - `npm run migrate:status` — 적용/대기 현황 확인.
   - 스키마 변경 시 — `npm run migrate:make <name>` 으로 새 `migrations/00N_*.sql` 생성 후 작성하고 `migrate`.
     마이그레이션 SQL은 **idempotent**하게 (MySQL DDL은 암묵 커밋이라 파일 단위 원자성이 없음).
   - `npm run db:init` — **파괴적 리셋**(모든 테이블 드롭). 초기화하려면 `db:init` → `migrate` 순서.
   - `npm run db:wipe` — 스키마는 유지하고 **데이터만** 전부 비움(TRUNCATE·AUTO_INCREMENT 리셋). 봇 오염 청소용, production에서는 거부.
5. **서버 실행**:
   ```
   npm run dev   # tsx watch (변경 자동 재시작)
   npm start     # 단발 실행
   ```
   - `GET /healthz` — 라이브니스(프로세스 생존, DB 미검사).
   - `GET /readyz` — 레디니스(DB ping 성공 시 200, 실패 시 503).
6. **테스트**:
   - `npm test` — vitest 단위 테스트(`tests/`, DB 불필요 순수 모듈만. watch는 `npm run test:watch`).
   - DB·프로세스를 실제로 쓰는 시나리오 하네스는 루트 [README](../README.md)의 "테스트 하네스" 표 참조.

## 엔드포인트

### `POST /api/auth/register`
```json
{ "loginId": "test01", "password": "abcd1234", "nickname": "테스트" }
```
성공 (토큰 미발급 — 로그인 별도 호출):
```json
{ "ok": true, "data": { "userId": 1, "nickname": "테스트", "score": 1000, "level": 1, "exp": 0 } }
```

### `POST /api/auth/login`
```json
{ "loginId": "test01", "password": "abcd1234" }
```
성공 (위 필드 + `token`):
```json
{ "ok": true, "data": { "userId": 1, "nickname": "테스트", "score": 1000, "level": 1, "exp": 0, "token": "eyJ..." } }
```

### `GET /api/auth/me`  · `Authorization: Bearer <token>`
토큰 검증 + 최신 프로필(score/level/exp) 반환.

### `GET /api/auth/heartbeat`  · `Authorization: Bearer <token>`
토큰 유효성만 확인 — `{ "ok": true, "data": { "valid": true } }` (프로필 조회 없음).

### `GET /api/ranking?limit=&offset=`  · `Authorization: Bearer <token>`
score DESC 순위 페이지. `limit` 기본 50·최대 100(초과 시 클램프), `offset` 기본 0.
```json
{ "ok": true, "data": { "entries": [ { "rank": 1, "userId": 1, "nickname": "…", "score": 1200, "level": 3, "wins": 5, "losses": 2, "matchesPlayed": 7 } ], "meta": { "total": 42, "limit": 50, "offset": 0 } } }
```

### `GET /api/match/current`  · `Authorization: Bearer <token>`
진행 중인 내 매치의 재입장 주소. `match:found` 푸시는 1회성이라 그 순간 끊기면 복구 수단이 없다 — 클라가 로비 진입 시 1회 호출한다.
결과가 이미 저장됐거나 본인이 탈주로 정산된 매치는 대상에서 제외한다(끝난 경기·DS가 입장을 거절할 대상으로 되돌려 보내지 않는다).
```json
{ "ok": true, "data": { "active": true, "matchId": "…", "server": { "host": "127.0.0.1", "port": 7777 }, "joinToken": "…" } }
{ "ok": true, "data": { "active": false } }
```

### `POST /api/match/result`  · `Authorization: Bearer <serverToken>` (DS 전용)
매치 결과 보고. **클라이언트 직접 호출 금지** — Dedicated Server가 매치별로 발급된 serverToken으로만 호출. ELO 점수 갱신 + 멱등 처리(`matchId` UNIQUE → 재제출 시 409).
결과가 끝내 오지 않은 매치는 만료 시 `end_reason='abort'`로 기록만 남는다(등수·점수 변동 없음).

### DS 전용 매치 수명주기 API  · `Authorization: Bearer <serverToken>`
`result`와 같은 서버 토큰 인증. DS가 매치 진행 중에 호출한다.

| METHOD + PATH | 용도 |
|---|---|
| `POST /api/match/:matchId/ready` | DS 기동 완료 콜백 — 백엔드가 이를 받아야 4명에게 서버 주소를 푸시 |
| `POST /api/match/:matchId/started` | 실제 플레이 시작 통지 — 이후 신규 입장 차단 |
| `GET /api/match/:matchId/kicks` | 강퇴 대상 폴링 → `{ "userIds": [...] }` |
| `POST /api/match/:matchId/leaver` | 탈주자 즉시 정산 — body `{ "userId" }` → `{ "userId", "scoreDelta", "scoreAfter" }` |

### `WS /ws/match`  · `Authorization: Bearer <JWT>`
매칭 큐 WebSocket(같은 host:port에서 업그레이드). 클라 메시지 `queue:join` / `queue:cancel`, 서버 푸시 `queue:joined` / `queue:left` / `match:found` / `error` / `session:invalid`(다른 곳 로그인으로 세션이 대체되면 푸시 후 close 4001).

### 에러 응답 (envelope)
```json
{ "ok": false, "error": { "code": "DUPLICATE_LOGIN_ID", "message": "이미 사용 중인 ID입니다." } }
```

| code | HTTP | 의미 |
|---|---|---|
| `VALIDATION_FAILED` | 400 | 입력 형식 위반 |
| `INVALID_RESULT` | 400 | 매치 결과 본문 형식 위반 |
| `BAD_MESSAGE` | 400 | WS 메시지 형식 위반 |
| `INVALID_CREDENTIALS` | 401 | ID/PW 불일치 |
| `AUTH_REQUIRED` / `INVALID_TOKEN` / `TOKEN_EXPIRED` | 401 | 토큰 누락 / 위변조 / 만료 |
| `SESSION_SUPERSEDED` | 401 | 다른 곳 로그인으로 세션 대체(단일 세션 강제) |
| `SERVER_AUTH_REQUIRED` | 401 | 서버 토큰 누락(결과 보고) |
| `INVALID_SERVER_TOKEN` | 403 | 서버 토큰 불일치 |
| `NOT_FOUND` / `MATCH_NOT_FOUND` | 404 | 리소스 / 매치 없음 |
| `DUPLICATE_LOGIN_ID` / `DUPLICATE_NICKNAME` | 409 | 회원가입 시 중복 |
| `ALREADY_IN_QUEUE` | 409 | 이미 매칭 큐에 있음 |
| `RESULT_ALREADY_SUBMITTED` | 409 | 이미 처리된 매치 결과 |
| `RATE_LIMITED` | 429 | 속도 제한 |
| `INTERNAL_ERROR` | 500 | 서버 오류 |

## 4-레이어

`network(router) → handler → service → repository`. 레이어 건너뛰는 호출 금지.
공통 유틸은 `common/`(rate-limit 팩토리·Bearer 추출·DB 트랜잭션/단건 조회 헬퍼·검증·에러).

## 서버 권위 모델

매치 결과/점수는 **DS만** 보고 가능(매치별 serverToken 검증). 클라이언트의 점수·순위·보상 직접 POST 금지.
