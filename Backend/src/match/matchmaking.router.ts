// 플레이어용 매치 라우터 (network 레이어) — 유저 JWT 인증.
// DS용 라우터(dsApi.router)와 같은 /api/match에 마운트되지만 인증 주체가 다르다:
// 여기는 requireAuth(유저 JWT), 저기는 매치별 serverToken. 경로가 겹치지 않도록 이 라우터를 먼저 태운다.
import express from 'express';

import { requireAuth } from '../common/authMiddleware.js';
import { config } from '../common/config.js';
import { createRateLimiter } from '../common/rateLimit.js';
import * as handler from './matchmaking.handler.js';

// 로그인 직후 1회 호출이라 랭킹과 같은 한도로 충분하다.
const currentLimiter = createRateLimiter(config.rateLimit.rankingMax);

const router = express.Router();

router.get('/current', currentLimiter, requireAuth, handler.current);

export default router;
