# D1 Backend

봄버맨 PvP 백엔드 (Node.js + Express + ws + MySQL). v0.1.0은 **인증 슬라이스 (회원가입/로그인)** 만 포함.

## 빠른 시작

1. **MySQL 비밀번호 설정** — `Backend/.env` 의 `DB_PASS=` 에 로컬 MySQL `root` 비밀번호를 적는다.
2. **JWT 시크릿 교체** — `JWT_SECRET=` 을 32바이트 이상의 임의 문자열로 바꾼다.
3. **DB 스키마 적용** (한 번만):
   ```
   npm run db:init
   ```
   MySQL `root` 비밀번호 입력 프롬프트가 뜬다. `d1` 데이터베이스 + `users` 테이블 생성.
4. **서버 실행**:
   ```
   npm run dev   # nodemon (변경 자동 재시작)
   npm start     # 단발 실행
   ```
   `http://127.0.0.1:3000/` 가 헬스체크.

## 엔드포인트 (v0.1.0)

### `POST /api/auth/register`
```json
{ "loginId": "test01", "password": "abcd1234", "nickname": "테스트" }
```
성공:
```json
{ "ok": true, "data": { "userId": 1, "nickname": "테스트", "score": 1000, "token": "eyJ..." } }
```

### `POST /api/auth/login`
```json
{ "loginId": "test01", "password": "abcd1234" }
```
응답 형식 동일.

### 에러 응답 (envelope)
```json
{ "ok": false, "error": { "code": "DUPLICATE_LOGIN_ID", "message": "이미 사용 중인 ID입니다." } }
```

| code | HTTP | 의미 |
|---|---|---|
| `VALIDATION_FAILED` | 400 | 입력 형식 위반 |
| `INVALID_CREDENTIALS` | 401 | ID/PW 불일치 |
| `DUPLICATE_LOGIN_ID` | 409 | 회원가입 시 ID 중복 |
| `DUPLICATE_NICKNAME` | 409 | 회원가입 시 닉네임 중복 |
| `RATE_LIMITED` | 429 | 속도 제한 |
| `INTERNAL_ERROR` | 500 | 서버 오류 |

## 4-레이어

`network(router) → handler → service → repository`. 레이어 건너뛰는 호출 금지.

## 다음 슬라이스 (예정)

- WebSocket (`ws`) 매칭 큐 — `/api/match/*` + WS 메시지 프로토콜
- 매치 결과 수신 (DS → 백엔드, 클라 직접 POST 금지)
- 랭킹 조회
