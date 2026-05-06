# Top-Down 4-Player PvP — Project Plan

언리얼 Dedicated Server를 활용한 4인 탑다운 개인전 PvP 게임.

---

## 프로젝트 구성

- **장르** — 탑다운 4인 개인전 PvP (랭킹 대전)
- **개발 기간** — 2주

---

## Feature 1. Login / Register

- ID/Password 입력 방식. nickname과 ID는 별개
- HTTP로 백엔드 통신
  - `POST /api/auth/login`
  - `POST /api/auth/register`
- 비밀번호는 bcrypt 해싱 후 저장 (평문 저장 금지)
- 성공 시 JWT 토큰 발급, 클라가 보관 후 요청마다 헤더로 전송

---

## Feature 2. Matching

### 핵심 결정

- **v1 — 랭킹 대전만** (친선전은 v2)
- **백엔드에 매칭 큐 직접 구현** — 게임 서버 직군 어필의 핵심 영역
- **Steam OSS는 세션 관리 껍데기로만 사용** (매칭 알고리즘은 직접 구현)

### 통신

- 클라 ↔ 백엔드 — **WebSocket**으로 큐 입장/취소/매칭 푸시
- 매칭 성사 시 백엔드가 빈 Dedicated Server 할당 → WebSocket으로 4명에게 서버 정보 푸시

### 매칭 알고리즘

- **자료구조** — 점수 정렬 배열 + `joinedAt` 필드
  - 형태: `{ userId, score, joinedAt, ws }`
  - score 오름차순 정렬 유지 (이진 탐색용)
- **점수 윈도우 확장 방식**
  - `window = base + waitSec × expandRate`
  - MAX_WINDOW로 상한 캡
- **seed 기반 매칭 시도**
  - 가장 오래 기다린 사람을 seed로 선택
  - seed 기준 윈도우 안의 후보 검색 → 4명 이상이면 가장 오래 기다린 4명 선정
  - **선착순 우대가 별도 로직 없이 자연 보장됨**
- 1초 주기로 매칭 사이클 실행
- 파라미터(BASE_WINDOW, EXPAND_RATE, MAX_WINDOW)는 설정 파일로 외부화

### v1 단순화

- 매칭 성사 시 **자동 입장** (수락 단계 없음)
- 매칭 취소 — WebSocket 메시지 또는 연결 끊김 시 큐에서 제거

---

## Feature 3. Ranking

- **점수 갱신** — 매치 종료 즉시 (이벤트 기반)
- **랭크보드 조회** — 매번 DB 조회 (v1)
  - v2: 캐싱 또는 Redis ZSET 도입
- DB는 MySQL

---

## Feature 4. Security

- **서버 권위 모델**
  - 매치 결과는 Dedicated Server만 백엔드로 전송 가능
  - DS에 별도 서버 토큰 발급, 백엔드가 검증
  - 클라가 직접 결과 POST 시도 → 401 거부 (시연 영상 제작 예정)
- **Rate Limiting** — `express-rate-limit` 적용
  - 로그인 엔드포인트
  - 매치 결과 엔드포인트
- **비밀번호 해싱** — bcrypt
- **JWT** — 만료 시간 검증, 위변조 검증
- **시크릿 관리** — DB 비번/JWT 시크릿은 `.env`로 분리, `.env.example`만 커밋

---

## Architecture

### Client (Unreal)

- 백엔드와 **HTTP + WebSocket 동시 사용** (같은 host:port)
- HTTP — 로그인, 회원가입, 랭킹 조회 (`FHttpModule`)
- WebSocket — 매칭 큐 입장/취소, 매칭 성사 푸시 수신 (`IWebSocketsModule`)
- `UBackendSubsystem` (GameInstanceSubsystem)이 양쪽 연결과 토큰 관리
- WebSocket 끊김 시 지수 백오프로 재연결
- 토큰으로 동일 사용자 식별

### Dedicated Server (Unreal)

- 매치 세션은 GameState로 관리
- 클라 입력 → Server RPC → 검증 → Replication → RepNotify 사이클
- 매치 종료 시 백엔드에 **HTTP POST** `/api/match/result` (서버 토큰 인증)
- WebSocket 안 씀 (DS는 푸시 받을 일 없음)

### Backend (Node.js + Express + ws + MySQL)

- **단일 프로세스, 단일 포트(3000)** 에서 HTTP + WebSocket 처리
- 같은 `http.Server` 객체를 Express와 ws가 공유 → Upgrade 헤더로 분기
- **모놀리스, 모듈 단위 폴더 분리**
  - `src/auth/` — 로그인, 회원가입, 토큰
  - `src/match/` — 매칭 큐, 매치 결과 수신
  - `src/ranking/` — 랭킹 조회
  - `src/common/` — DB, 로깅, 설정
- **4-레이어 구조** — network / handler / service / repository
- 향후 분리 가능한 경계로 설계 (운영 시 ranking, match 분리 가능)

---

## Database Schema

> v1은 핵심 테이블 3개로 시작. `refresh_tokens`는 선택 사항(시간 남으면).

### users — 인증 + 현재 점수 통합

```sql
CREATE TABLE users (
  id              BIGINT       NOT NULL AUTO_INCREMENT,
  login_id        VARCHAR(64)  NOT NULL,
  password_hash   VARCHAR(255) NOT NULL,
  nickname        VARCHAR(32)  NOT NULL,
  score           INT          NOT NULL DEFAULT 1000,
  created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
                               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_login_id (login_id),
  UNIQUE KEY uk_nickname (nickname),
  KEY idx_score (score DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

**설계 메모:**

- `id`는 BIGINT — 확장성 의식
- `login_id`와 `nickname` 분리 — 닉네임만 노출, 로그인 ID는 비공개
- `password_hash` VARCHAR(255) — bcrypt 60자 + 알고리즘 변경 여유
- `idx_score (score DESC)` — 랭킹 조회 풀스캔 방지
- 별도 `rankings` 테이블을 두지 않은 이유 — 점수가 users 한 군데에서만 갱신되므로 동기화 부담만 늘어남. 대신 인덱스로 해결.

### matches — 매치 자체 정보

```sql
CREATE TABLE matches (
  id              BIGINT       NOT NULL AUTO_INCREMENT,
  mode            VARCHAR(16)  NOT NULL DEFAULT 'ranked',
  started_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  ended_at        TIMESTAMP    NULL,
  duration_sec    INT          NULL,
  winner_user_id  BIGINT       NULL,
  PRIMARY KEY (id),
  KEY idx_started_at (started_at),
  KEY idx_winner (winner_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

**설계 메모:**

- `mode` 컬럼 — v2 친선전 대비. 추가 모드는 문자열 값만 추가하면 됨
- `ended_at` NULL 허용 — DS 크래시 등 미종료 매치 표현
- `duration_sec` — 매번 계산 대신 저장해서 통계 쿼리 빠르게
- `winner_user_id` — 4인 개인전 1등. `match_participants.placement = 1`로도 추론 가능하지만 인덱스로 즉답 가능하도록 별도 보관

### match_participants — 매치별 참여자 + 결과

```sql
CREATE TABLE match_participants (
  match_id        BIGINT      NOT NULL,
  user_id         BIGINT      NOT NULL,
  placement       TINYINT     NOT NULL,  -- 1=1등 ... 4=4등
  kills           INT         NOT NULL DEFAULT 0,
  score_delta     INT         NOT NULL,  -- 점수 변화 (+25, -15 등)
  score_after     INT         NOT NULL,  -- 매치 후 점수
  PRIMARY KEY (match_id, user_id),
  KEY idx_user_recent (user_id, match_id DESC),
  KEY idx_user_placement (user_id, placement)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

**설계 메모:**

- PK `(match_id, user_id)` — 한 매치에 같은 유저 중복 방지 자동 보장
- `placement` TINYINT — 1~4 값이라 INT는 낭비
- `score_delta` + `score_after` 동시 보관 — 점수 변화 감사(audit) 추적 가능
- `idx_user_recent (user_id, match_id DESC)` — "내 최근 전적" 화면용. 정렬까지 인덱스로 흡수
- `idx_user_placement (user_id, placement)` — "1등 몇 번?" 류 통계용

### refresh_tokens — 선택 사항

JWT 강제 로그아웃이 필요할 때만 도입. v1에서는 짧은 만료 시간(15분)으로 우회.

```sql
CREATE TABLE refresh_tokens (
  id              BIGINT       NOT NULL AUTO_INCREMENT,
  user_id         BIGINT       NOT NULL,
  token_hash      VARCHAR(255) NOT NULL,
  expires_at      TIMESTAMP    NOT NULL,
  revoked         BOOLEAN      NOT NULL DEFAULT FALSE,
  created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_user (user_id),
  KEY idx_token_hash (token_hash)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### 트랜잭션 처리

매치 결과 저장은 3개 테이블 변경을 atomic하게:

```javascript
// MatchService.saveResult
async saveResult(matchData) {
  const conn = await pool.getConnection();
  try {
    await conn.beginTransaction();

    // 1. matches 행 생성
    const [matchResult] = await conn.query(
      'INSERT INTO matches (mode, started_at, ended_at, duration_sec, winner_user_id) VALUES (?, ?, ?, ?, ?)',
      [...]
    );
    const matchId = matchResult.insertId;

    // 2. participants 4행 + users.score 갱신
    for (const p of matchData.participants) {
      await conn.query(
        'INSERT INTO match_participants (match_id, user_id, placement, kills, score_delta, score_after) VALUES (?, ?, ?, ?, ?, ?)',
        [matchId, p.userId, p.placement, p.kills, p.scoreDelta, p.scoreAfter]
      );
      await conn.query(
        'UPDATE users SET score = ? WHERE id = ?',
        [p.scoreAfter, p.userId]
      );
    }

    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
}
```

### 확장 시나리오

- **트래픽 증가** — matches는 시간 기반 파티셔닝(월별), users는 사용자 수 폭증 시 샤딩
- **랭킹 조회 부하** — Redis ZSET을 캐시로 추가. users.score 변경 시 ZSET도 갱신
- **친선전 추가** — matches.mode에 `'friendly'` 값 추가. 친선전은 점수 갱신 안 함
- **시즌 도입** — matches에 season_id 컬럼 추가. 별도 user_season_stats 테이블에 시즌별 점수
- **상세 통계 추가** — match_participants에 컬럼 추가 (damage_dealt 등)

---

## Claude Code + MCP 협업 환경

이 프로젝트는 단순한 코드 생성을 넘어, AI 협업 워크플로우 자체를 설계.

- **monolith MCP** — Claude Code로 언리얼 에디터 조작 (클라/DS 작업)
- **자체 MCP 서버 (직접 작성)** — Claude Code가 백엔드 상태 조회/조작
  - 노출 도구 예시 — `get_match_queue_state`, `simulate_user_join`, `inspect_player`, `reset_db_for_test`
  - 매칭 알고리즘 테스트 시나리오를 자연어로 기술하면 자동 실행
- **CLAUDE.md** — 프로젝트 컨벤션, 폴더 구조, 핵심 규칙 명문화
- **Subagent**
  - `server-security-reviewer` — 백엔드 코드 보안 위주 리뷰
  - `unreal-cpp-reviewer` — 언리얼 C++ 위생 점검 (UPROPERTY, GC, Replication)
- **테스트 하네스** — 매칭 알고리즘 시나리오 기반 단위 테스트
- **Plan Mode** — 새 모듈 시작 시 기본 습관

---

## 폴더 구조 (예정)

```
project-root/
├── Client/                 # 언리얼 클라이언트 프로젝트
├── Server/                 # 언리얼 Dedicated Server 빌드 산출물
├── Backend/                # Node.js 백엔드
│   ├── src/
│   │   ├── auth/
│   │   ├── match/
│   │   ├── ranking/
│   │   ├── common/
│   │   └── main.js
│   ├── tests/              # 매칭 알고리즘 테스트 하네스
│   ├── package.json
│   ├── .env.example
│   └── README.md
├── BackendMcp/             # 자체 작성 MCP 서버
├── docs/                   # 아키텍처 다이어그램, 설계 문서
├── CLAUDE.md               # Claude Code 프로젝트 컨텍스트
└── README.md
```

---

## 마일스톤

### Week 1 — 코어 멀티플레이 + 백엔드 골격

- **Day 1–2** — Dedicated Server 빌드 세팅, 두 클라가 같은 방 입장 확인
- **Day 3–4** — 캐릭터 이동/체력 Replication, Server RPC 데미지 검증, RepNotify
- **Day 5–6** — Node.js 백엔드 골격, MySQL 연결, 회원가입/로그인 (HTTP)
- **Day 7** — WebSocket 추가, 매칭 큐 자료구조 + 알고리즘 구현

### Week 2 — 매칭 통합 + 보안 + 마무리

- **Day 8–9** — 매칭 성사 → DS 입장 흐름 통합, 매치 결과 저장 트랜잭션
- **Day 10** — 자체 MCP 서버 작성 (최소 1~2개 도구), 매칭 테스트 하네스
- **Day 11** — 보안 시연 (서버 권위, Rate Limit), Subagent 정의
- **Day 12** — 통합 테스트, 버그 수정
- **Day 13–14** — README, 데모 영상(서버 콘솔 포함), 아키텍처 다이어그램

---

## 미해결 / 추후 결정 항목

- 점수 갱신 알고리즘 — ELO 방식 vs 단순 +/- 룰
- 자체 MCP 서버에 노출할 도구의 최종 범위
- CLAUDE.md 구체 내용
- Subagent 정의 파일 작성

---

## 참고

- 친선전은 v2로 보류
- Redis 도입은 v2로 보류 (시간 남으면 v1에 랭킹 ZSET만 추가)
- 매칭 수락 단계는 v2 (v1은 자동 입장)
