// 랭킹 라우터 (network 레이어) — rate-limit + requireAuth 부착
import express from 'express';

import { requireAuth } from '../common/authMiddleware.js';
import { config } from '../common/config.js';
import { createRateLimiter } from '../common/rateLimit.js';
import * as handler from './ranking.handler.js';

const rankingLimiter = createRateLimiter(config.rateLimit.rankingMax);

const router = express.Router();

router.get('/', rankingLimiter, requireAuth, handler.getRanking);

export default router;
