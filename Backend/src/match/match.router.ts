// 매치 라우터 (network 레이어) — 결과 보고 엔드포인트에 rate-limit 부착.
import express from 'express';

import { config } from '../common/config.js';
import { makeRateLimiter } from '../common/rateLimit.js';
import * as handler from './match.handler.js';

const resultLimiter = makeRateLimiter(config.rateLimit.resultMax);

const router = express.Router();

router.post('/result', resultLimiter, handler.submitResult);

export default router;
