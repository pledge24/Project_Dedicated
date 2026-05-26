// D1 백엔드 엔트리: env 로드 → app 구성 → listen
require('dotenv').config();

const buildApp = require('./app');

const PORT = Number(process.env.PORT) || 3000;

const app = buildApp();

app.listen(PORT, () => {
    console.log(`[D1 Backend] listening on http://127.0.0.1:${PORT}`);
});
