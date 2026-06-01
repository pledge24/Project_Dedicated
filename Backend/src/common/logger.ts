// 공용 pino 로거. dev는 pino-pretty(컬러), prod는 JSON→stdout(동기).
import pino from 'pino';
import type { LoggerOptions } from 'pino';

import { config } from './config.js';

const isProd = config.nodeEnv === 'production';

const options: LoggerOptions = {
    level: config.logLevel,
    // 토큰/쿠키는 로그에서 제거 (pino-http 요청 로그에도 적용됨)
    redact: { paths: ['req.headers.authorization', 'req.headers.cookie'], remove: true },
    // dev만 pretty. prod는 transport 없이 JSON을 fd1로 동기 출력.
    ...(isProd
        ? {}
        : { transport: { target: 'pino-pretty', options: { translateTime: 'SYS:standard', ignore: 'pid,hostname' } } }),
};

export const logger = pino(options);
