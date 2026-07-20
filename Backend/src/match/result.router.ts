// 결과 라우터 (network 레이어) — 결과 보고 엔드포인트에 rate-limit 부착.
import express from 'express';

import { config } from '../common/config.js';
import { makeRateLimiter } from '../common/rateLimit.js';
import * as handler from './result.handler.js';

const resultLimiter = makeRateLimiter(config.rateLimit.resultMax);
const pollLimiter = makeRateLimiter(config.rateLimit.pollMax);

const router = express.Router();

router.post('/result', resultLimiter, handler.submitResult);
router.get('/:matchId/kicks', pollLimiter, handler.getKicks);

export default router;
