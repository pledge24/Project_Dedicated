// 인증 미들웨어(requireAuth)가 req.user 에 토큰 클레임을 주입한다.
// Express 기본 Request 에는 user 가 없으므로 여기서 타입을 확장한다.
import 'express';

declare global
{
    namespace Express
    {
        interface Request
        {
            user?: import('../common/types.js').AuthedUser;
        }
    }
}
