// 봇전(Bot-Fill) 봇 생성 — 순수 함수. DB에 존재하지 않는 가상의 상대.
// 서버 권위: rating(ELO 입력)은 백엔드가 소유하고 DS엔 넘기지 않는다(DS는 결과만 보고).
// sentinel userId는 음수(-1,-2,-3): roster는 매치 스코프라 매치마다 재사용해도 충돌 없음.

/** 생성된 봇 1명. userId는 음수 sentinel(실제 유저와 구분·DB 미존재), rating은 ELO 입력용. */
export interface BotOpponent
{
    userId: number;
    nickname: string;
    rating: number;
}

// 봇 닉네임 풀 — 카드에 BOT 배지와 함께 표기(닉네임 규칙과 무관, roster로만 사용·DB 미기록).
const BOT_NAMES = ['Arden', 'Luna', 'Milo', 'Nova', 'Kai', 'Iris', 'Rex', 'Zoe', 'Bolt', 'Echo'];

/**
 * 대기 유저 1명을 위한 봇 count명 생성. rating = humanScore ± rand(spread), floor/ceiling clamp.
 * rand는 순수성 위해 주입 가능(테스트 결정론); 미주입 시 Math.random.
 */
export function createBotOpponents(
    humanScore: number,
    count: number,
    spread: number,
    floor: number,
    ceiling: number,
    rand: () => number = Math.random
): BotOpponent[]
{
    const names = pickNames(count, rand);
    const bots: BotOpponent[] = [];
    for (let i = 0; i < count; i++)
    {
        const offset = Math.round((rand() * 2 - 1) * spread); // [-spread, +spread]
        bots.push({
            userId: -(i + 1),
            nickname: `Bot ${names[i]}`,
            rating: Math.min(ceiling, Math.max(floor, humanScore + offset)),
        });
    }

    return bots;
}

/** 풀에서 count개 이름을 순환 선택(count <= 풀 크기면 중복 없음). rand 오프셋으로 매치마다 달라짐. */
function pickNames(count: number, rand: () => number): string[]
{
    const start = Math.floor(rand() * BOT_NAMES.length);
    const out: string[] = [];
    for (let i = 0; i < count; i++)
    {
        out.push(BOT_NAMES[(start + i) % BOT_NAMES.length]);
    }

    return out;
}
