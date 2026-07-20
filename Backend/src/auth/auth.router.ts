// 인증 라우터 (network 레이어) — rate-limit 부착
import express from 'express';

import { requireAuth } from '../common/authMiddleware.js';
import { config } from '../common/config.js';
import { makeRateLimiter } from '../common/rateLimit.js';
import * as handler from './auth.handler.js';

const loginLimiter = makeRateLimiter(config.rateLimit.loginMax);
const registerLimiter = makeRateLimiter(config.rateLimit.registerMax);

const router = express.Router();

router.post('/register',  registerLimiter, handler.register);
router.post('/login',     loginLimiter,    handler.login);
router.get('/me',         requireAuth,     handler.me);
router.get('/heartbeat',  requireAuth,     handler.heartbeat);

export default router;
