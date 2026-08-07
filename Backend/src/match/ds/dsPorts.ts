// DS 포트 풀 — 매치당 빈 UDP 포트 하나를 선점/해제하고 외부 점유를 프로브한다.
// ds.ts에서 분리한 이유: 포트는 프로세스 수명과 별개로 관리되는 자원이다.
// 가동 중인 포트가 무엇인지는 ds.ts의 running Map만 알므로 isInUse 콜백으로 물어본다.
import dgram from 'node:dgram';

import { config } from '../../common/config.js';
import { logger } from '../../common/logger.js';

const ds = config.match.ds;
// 포트 프로브가 비동기라 "프로브 통과 → running.set" 사이에 다른 allocate가 끼어들 수 있다.
// runMatchCycle이 handleMatch를 동시에 여러 개 띄우므로(ws.ts) 후보 포트를 동기적으로 선점해 둔다.
const reserved = new Set<number>();

/**
 * 빈 포트 하나를 선점해 반환. 없으면 null.
 * 3중 확인 — isInUse(내가 쓰는 중) + reserved(다른 allocate가 선점 중) + UDP 프로브(외부 점유).
 * 프로브가 필요한 이유: 가동 목록만 믿으면 이전 실행의 고아 DS가 쥔 포트를 재배정하고,
 * 새 DS는 bind에 실패해 증상이 30초 뒤 readiness 타임아웃으로만 나타난다(원인 추적 불가).
 */
export async function reserveFreePort(isInUse: (port: number) => boolean): Promise<number | null>
{
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (isInUse(p) || reserved.has(p))
        {
            continue;
        }

        // await 앞에서 동기 선점 — 프로브 대기 중 다른 allocate가 같은 포트를 집어가는 것을 막는다.
        reserved.add(p);
        if (await isPortAvailable(p))
        {
            return p;
        }

        reserved.delete(p);
        logger.warn({ port: p }, 'DS 포트가 외부 프로세스에 점유됨 — 건너뜀');
    }

    return null;
}

/** 선점 해제. 프로세스 등재에 성공했든 spawn이 실패했든 불러야 한다(예약 누수 = 풀 축소). */
export function releaseReservation(port: number): void
{
    reserved.delete(port);
}

/**
 * 부팅 시 포트 풀 점검 — 이전 실행이 크래시로 남긴 고아 DS를 로그로 드러낸다.
 * 죽이지는 않는다: exe 이름 기준 일괄 taskkill은 개발자가 디버깅용으로 직접 띄운 DS까지 죽인다.
 * 점유된 포트는 reserveFreePort의 프로브가 매번 걸러내므로, 여기서는 원인 추적용 기록만 남기면 충분하다.
 */
export async function reapOrphans(): Promise<void>
{
    const occupied: number[] = [];
    for (let p = ds.portMin; p <= ds.portMax; p++)
    {
        if (!(await isPortAvailable(p)))
        {
            occupied.push(p);
        }
    }

    if (occupied.length > 0)
    {
        logger.warn({ occupied, portMin: ds.portMin, portMax: ds.portMax },
            'DS 포트 점유 감지 — 이전 실행의 고아 프로세스일 수 있음. 해당 포트는 할당에서 제외된다');
    }
}

/**
 * 포트 사용 가능 여부를 UDP로 프로브.
 * UE의 -port는 IpNetDriver 게임 트래픽 포트라 UDP다 — TCP(net.createServer)로 확인하면
 * DS가 점유 중인 포트도 "비었다"고 나와 프로브가 무의미해진다.
 * dgram의 reuseAddr 기본값이 false라 점유된 포트는 EADDRINUSE로 error 이벤트가 뜬다.
 * 주소를 지정하지 않아 와일드카드(0.0.0.0)로 바인드 — DS가 어느 인터페이스에 붙었든 검출된다.
 */
function isPortAvailable(port: number): Promise<boolean>
{
    return new Promise((resolve) =>
    {
        const probe = dgram.createSocket('udp4');
        probe.once('error', () =>
        {
            resolve(false);
        });

        probe.once('listening', () =>
        {
            probe.close(() =>
            {
                resolve(true);
            });
        });
        probe.bind(port);
    });
}
