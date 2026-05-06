# CLAUDE.md

Claude Code가 이 프로젝트에서 일관되게 작업하도록 컨텍스트를 제공한다.
실제 요구사항/설계 디테일은 `PROJECT_PLAN.md`를 참조.

---

## Project Overview

Top-Down 4-Player PvP — 언리얼 Dedicated Server + 자체 백엔드(매칭/인증/랭킹) 기반 랭킹 대전 게임.

- 상세 기획/설계: [`PROJECT_PLAN.md`](./PROJECT_PLAN.md)

---

## Folder Structure

```
Project_Dedicated/
├── D1/                # 언리얼 프로젝트 (클라/DS 공용 소스)
│   └── Source/...
├── Builds/            # 패키징 산출물 (.gitkeep만 커밋)
│   ├── Client/
│   └── Server/
├── Backend/           # Node.js + Express + ws
│   ├── src/
│   │   ├── auth/
│   │   ├── match/
│   │   ├── ranking/
│   │   └── common/
│   └── tests/
├── docs/              # 아키텍처 다이어그램, 설계 문서
├── PROJECT_PLAN.md
├── CLAUDE.md
└── README.md
```

> `BackendMcp/`는 자체 MCP 서버 자리 (추후 추가).

---

## Tech Stack

- **Client / Dedicated Server**: Unreal Engine 5.7 (C++ 모듈명 `D1`)
- **Backend**: Node.js + Express + `ws` (WebSocket) + `mysql2`
- **DB**: MySQL (스키마는 PROJECT_PLAN.md 참조)
- **Auth**: bcrypt + JWT
- **Comm**: HTTP (`FHttpModule`) + WebSocket (`IWebSocketsModule`) — 같은 host:port

---

## Conventions

> TODO: 코드 스타일, 네이밍, 커밋 컨벤션 결정 후 채우기.

- C++ — Unreal 표준(접두어 `U`/`A`/`F`/`E`, PascalCase)
- JS — TBD (ESLint 설정 추가 시 결정)
- Commit — TBD

---

## Build & Run

> TODO: 명령 확정 후 채우기.

- **언리얼 클라**: TBD
- **Dedicated Server 빌드**: TBD (`D1Server.Target.cs` 추가 필요)
- **Backend 실행**: TBD (`npm run dev`)
- **DB 마이그레이션**: TBD

---

## Key Rules

> 본격 개발 전 채우기. 우선순위 높은 항목 먼저:

- **서버 권위 모델** — 매치 결과는 DS만 백엔드로 전송 (서버 토큰 검증). 클라 직접 POST 금지.
- **시크릿 관리** — `.env`는 절대 커밋 금지. `.env.example`만 커밋.
- **비밀번호** — 평문 저장 금지, bcrypt 해싱 필수.
- **Rate Limit** — 로그인/매치 결과 엔드포인트는 `express-rate-limit` 적용.

---

## Subagents (예정)

- `server-security-reviewer` — 백엔드 코드 보안 위주 리뷰 (JWT 검증, SQL 인젝션, Rate Limit 누락 등)
- `unreal-cpp-reviewer` — 언리얼 C++ 위생 (UPROPERTY, GC, Replication, RPC 검증)

---

## Working Notes

- Plan Mode를 새 모듈 시작 시 기본 습관으로 사용
- 매칭 알고리즘 변경은 자체 MCP 서버의 테스트 도구로 시나리오 검증 후 머지
