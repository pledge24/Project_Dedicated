// 인증 라우터 (network 레이어) — rate-limit 부착
import express from 'express';

import { requireAuth } from '../common/authMiddleware.js';
import { config } from '../common/config.js';
import { makeRateLimiter } from '../common/rateLimit.js';
import * as handler from './auth.handler.js';

const loginLimiter = makeRateLimiter(config.rateLimit.loginMax);
const registerLimiter = makeRateLimiter(config.rateLimit.registerMax);
// 인증 필요 경로도 제한 — requireAuth가 매 호출 DB SELECT(토큰 버전 대조)를 하므로 무제한이면 증폭 벡터가 된다.
const sessionLimiter = makeRateLimiter(config.rateLimit.sessionMax);

const router = express.Router();

router.post('/register',  registerLimiter, handler.register);
router.post('/login',     loginLimiter,    handler.login);
router.get('/me',         sessionLimiter,  requireAuth, handler.me);
router.get('/heartbeat',  sessionLimiter,  requireAuth, handler.heartbeat);

export default router;
