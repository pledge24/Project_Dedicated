// DS→백엔드 요청 라우터 (network 레이어) — 결과 보고 + kick 폴링 + 탈주 정산. /api/match 에 마운트.
import express from 'express';

import { config } from '../common/config.js';
import { createRateLimiter } from '../common/rateLimit.js';
import * as handler from './dsApi.handler.js';

const resultLimiter = createRateLimiter(config.rateLimit.resultMax);
// DS 폴링(:matchId/kicks)·정산(:matchId/leaver)은 다수 매치가 같은 host IP라 넉넉한 한도(pollMax).
const pollLimiter = createRateLimiter(config.rateLimit.pollMax);

const router = express.Router();

router.post('/result', resultLimiter, handler.submitResult);
router.post('/:matchId/ready', pollLimiter, handler.reportReady);
router.get('/:matchId/kicks', pollLimiter, handler.listKicks);
router.post('/:matchId/leaver', pollLimiter, handler.submitLeaver);

export default router;
