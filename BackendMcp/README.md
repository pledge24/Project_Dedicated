# BackendMcp

D1 백엔드 상태를 Claude Code에서 바로 조회하기 위한 **읽기 전용 MCP 서버**(stdio).

## 왜 DB만 읽는가

백엔드에 진단용 HTTP 엔드포인트를 여는 쪽이 정보는 더 많다. 그런데 이 프로젝트가 증명하려는 것이
"서버 권위"인데, 그걸 위해 서버에 새 공격면을 여는 건 값이 안 맞는다.

roster가 DB에 영속되므로(`match_rosters`) **진행 중인 매치까지는 DB만으로 답할 수 있다.**
답할 수 없는 것은 **아직 매칭되지 않은 대기열** 하나다 — 큐 엔트리가 살아있는 WebSocket 객체를
참조해서 프로세스 밖에서는 보이지 않는다. 그건 백엔드 로그로 본다.

## 도구

| 도구 | 답하는 질문 |
|---|---|
| `inspect_player` | "이 사람 지금 어떤 상태인가" — 점수·레벨·전적, 진행 중 매치, 최근 결과 |
| `list_active_matches` | "지금 몇 판이 돌고 있나" — DS 주소, 참가자, 경과 시간, 결과 보고 여부 |

`joinToken`·`serverToken`은 어느 응답에도 싣지 않는다(신원 도용·결과 위조 권한이라).

## 설정

DB 접속 정보는 `Backend/.env`와 같은 키를 쓴다(`DB_HOST`·`DB_PORT`·`DB_USER`·`DB_PASS`·`DB_NAME`).

```bash
cd BackendMcp
npm install
```

Claude Code에 등록 (`.mcp.json` 또는 `claude mcp add`):

```json
{
  "mcpServers": {
    "d1-backend": {
      "command": "node",
      "args": ["<repo>/BackendMcp/src/index.js"],
      "env": { "DB_PASS": "<mysql root 비밀번호>" }
    }
  }
}
```

`.env`를 `BackendMcp/`에 두어도 된다(dotenv 로드).
