// 인증 라우터 (network 레이어) — rate-limit 부착
const express = require('express');
const rateLimit = require('express-rate-limit');

const handler = require('./auth.handler');
const { fail } = require('../common/envelope');

const WINDOW_MS = Number(process.env.RATE_LIMIT_WINDOW_MS) || 60_000;
const LOGIN_MAX = Number(process.env.RATE_LIMIT_LOGIN_MAX) || 5;
const REGISTER_MAX = Number(process.env.RATE_LIMIT_REGISTER_MAX) || 10;

/** 공통 limit 핸들러 — envelope 포맷 응답 */
function rateLimitHandler(req, res)
{
    res.status(429).json(fail('RATE_LIMITED', '요청이 너무 잦습니다. 잠시 후 다시 시도해주세요.'));
}

const loginLimiter = rateLimit({
    windowMs: WINDOW_MS,
    max: LOGIN_MAX,
    standardHeaders: true,
    legacyHeaders: false,
    handler: rateLimitHandler,
});

const registerLimiter = rateLimit({
    windowMs: WINDOW_MS,
    max: REGISTER_MAX,
    standardHeaders: true,
    legacyHeaders: false,
    handler: rateLimitHandler,
});

const router = express.Router();

router.post('/register', registerLimiter, handler.register);
router.post('/login',    loginLimiter,    handler.login);

module.exports = router;
