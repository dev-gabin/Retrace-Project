import test from 'node:test';
import assert from 'node:assert/strict';
import { createApi, validateItem, ApiError } from '../api.js';

const record = (item = 'carkey') => ({ item, pos_x: 412, pos_y: 288, seen_at: '2026-10-03T14:22:05+09:00', snapshot: 'snapshots/key.jpg', drawer_id: null, state: 'visible' });
const reply = (data, status = 200) => ({ ok: status >= 200 && status < 300, status, json: async () => structuredClone(data) });
const hasCode = code => error => error instanceof ApiError && error.code === code;
function liveWith(data, status = 200) {
  const calls = [];
  const api = createApi({ mode: 'live', apiBaseUrl: 'http://jetson.local:8080/', fetchImpl: async (url, options) => {
    calls.push({ url, ...options });
    return reply(typeof data === 'function' ? data(url) : data, status);
  } });
  return { api, calls };
}

test('실제 모드는 문서의 모든 경로·메서드·본문을 사용한다', async () => {
  const { api, calls } = liveWith(url => url.endsWith('/api/items') ? { items: [record()] } : url.endsWith('/api/items/carkey') ? record() : { ok: true });
  assert.deepEqual(await api.listItems(), [record()]);
  assert.deepEqual(await api.getItem('carkey'), record());
  await api.aim('carkey');
  await api.openDrawer(6);
  await api.setBuzzer(true);
  await api.setBuzzer(false);
  assert.deepEqual(calls.map(({ url, method, body }) => [url, method, body]), [
    ['http://jetson.local:8080/api/items', 'GET', undefined],
    ['http://jetson.local:8080/api/items/carkey', 'GET', undefined],
    ['http://jetson.local:8080/api/items/carkey/aim', 'POST', '{}'],
    ['http://jetson.local:8080/api/drawers/6/open', 'POST', '{}'],
    ['http://jetson.local:8080/api/buzzer', 'POST', '{"enabled":true}'],
    ['http://jetson.local:8080/api/buzzer', 'POST', '{"enabled":false}'],
  ]);
  for (const call of calls) {
    assert.equal(call.headers.Accept, 'application/json');
    assert.equal(call.cache, 'no-store');
    assert.ok(call.signal instanceof AbortSignal);
    assert.equal(call.headers['Content-Type'], call.method === 'POST' ? 'application/json' : undefined);
  }
});

test('서버가 반환한 동적 물건 ID를 조회하고 안내할 수 있다', async () => {
  const keyboard = record('keyboard');
  const { api, calls } = liveWith(url => url.endsWith('/api/items') ? { items: [keyboard] } : url.endsWith('/api/items/keyboard') ? keyboard : { ok: true });
  assert.deepEqual(await api.listItems(), [keyboard]);
  assert.deepEqual(await api.getItem('keyboard'), keyboard);
  await api.aim('keyboard');
  assert.deepEqual(calls.map(call => call.url), [
    'http://jetson.local:8080/api/items',
    'http://jetson.local:8080/api/items/keyboard',
    'http://jetson.local:8080/api/items/keyboard/aim',
  ]);
});

test('샘플 모드는 모든 조회·제어에서 네트워크를 사용하지 않고 기록 복사본을 준다', async () => {
  let fetched = 0;
  const api = createApi({ fetchImpl: async () => { fetched++; throw new Error('network forbidden'); } });
  const items = await api.listItems();
  assert.deepEqual(items.map(item => item.item), ['carkey', 'airpods', 'wallet']);
  items[0].pos_x = 999;
  assert.equal((await api.getItem('carkey')).pos_x, 412);
  for (const item of items) await api.aim(item.item);
  for (let n = 1; n <= 6; n++) await api.openDrawer(n);
  await api.setBuzzer(true);
  await api.setBuzzer(false);
  assert.equal(fetched, 0);
});

test('잘못된 물건·서랍·부저 값은 요청 전에 거절한다', async () => {
  const { api, calls } = liveWith({ ok: true });
  for (const value of ['../carkey', 'CARKEY', '', null]) {
    await assert.rejects(api.getItem(value), hasCode('INVALID_ITEM'));
    await assert.rejects(api.aim(value), hasCode('INVALID_ITEM'));
  }
  for (const value of [0, 7, -1, 1.5, '1', null]) await assert.rejects(api.openDrawer(value), hasCode('INVALID_DRAWER'));
  for (const value of [0, 1, '0', '1', null]) await assert.rejects(api.setBuzzer(value), hasCode('INVALID_BUZZER'));
  assert.equal(calls.length, 0);
});

test('기록 없음·서랍 기록·UTC 시각·사진 없는 기록을 허용한다', () => {
  const unseen = { ...record(), pos_x: null, pos_y: null, seen_at: null, snapshot: null, state: 'uncertain' };
  assert.deepEqual(validateItem(unseen), unseen);
  const drawer = { ...record(), pos_x: null, pos_y: null, drawer_id: 3, snapshot: null, state: 'occluded' };
  assert.deepEqual(validateItem(drawer), drawer);
  assert.equal(validateItem({ ...record(), seen_at: '2026-10-03T05:22:05.123Z' }).seen_at, '2026-10-03T05:22:05.123Z');
});

const malformed = [
  ['없는 필드', { item: 'carkey' }],
  ['대문자 물건 ID', { ...record(), item: 'UNKNOWN_ITEM' }],
  ['음수 좌표', { ...record(), pos_x: -1 }],
  ['한쪽만 없는 좌표', { ...record(), pos_x: null }],
  ['소수 좌표', { ...record(), pos_y: 1.2 }],
  ['문자열 좌표', { ...record(), pos_x: '412' }],
  ['시간대 없음', { ...record(), seen_at: '2026-10-03T14:22:05' }],
  ['ISO가 아닌 시각', { ...record(), seen_at: 'Oct 3 2026 05:22:05Z' }],
  ['잘못된 시각', { ...record(), seen_at: '2026-99-03T14:22:05Z' }],
  ['모르는 상태', { ...record(), state: 'connected' }],
  ['범위 밖 서랍', { ...record(), drawer_id: 7 }],
  ['문자열 서랍', { ...record(), drawer_id: '3' }],
  ['기록 없이 남은 위치', { ...record(), seen_at: null }],
  ['외부 사진', { ...record(), snapshot: 'https://example.com/key.jpg' }],
  ['상위 경로', { ...record(), snapshot: 'snapshots/../key.jpg' }],
  ['역슬래시', { ...record(), snapshot: 'snapshots/dir\\key.jpg' }],
  ['사진 쿼리', { ...record(), snapshot: 'snapshots/key.jpg?x=1' }],
  ['빈 사진 이름', { ...record(), snapshot: 'snapshots/' }],
];
for (const [name, value] of malformed) test(`서버 기록 검사: ${name}`, () => assert.throws(() => validateItem(value), hasCode('BAD_RESPONSE')));

test('목록의 잘못된 구조·중복 ID와 다른 물건 상세를 거절한다', async () => {
  for (const data of [[record()], { items: null }, { items: [record(), record()] }]) {
    await assert.rejects(liveWith(data).api.listItems(), hasCode('BAD_RESPONSE'));
  }
  await assert.rejects(liveWith({ ...record(), item: 'wallet' }).api.getItem('carkey'), hasCode('BAD_RESPONSE'));
});

test('ok: true 접수 응답만 성공으로 처리한다', async () => {
  for (const data of [{ ok: false }, { ok: 'true' }, {}, null]) {
    const { api } = liveWith(data);
    await assert.rejects(api.aim('carkey'), hasCode('BAD_RESPONSE'));
    await assert.rejects(api.openDrawer(1), hasCode('BAD_RESPONSE'));
    await assert.rejects(api.setBuzzer(false), hasCode('BAD_RESPONSE'));
  }
});

test('장치 오류 응답의 코드·메시지·상태를 보존한다', async () => {
  const { api } = liveWith({ error: { code: 'DEVICE_OFFLINE', message: '서랍에 연결할 수 없습니다.' } }, 503);
  await assert.rejects(api.openDrawer(2), error => hasCode('DEVICE_OFFLINE')(error) && error.status === 503 && error.message === '서랍에 연결할 수 없습니다.');
});

test('HTTP 실패·JSON 오류·네트워크 실패를 샘플 성공으로 바꾸지 않는다', async () => {
  await assert.rejects(liveWith({}, 404).api.listItems(), hasCode('HTTP_404'));
  const nonJson = createApi({ mode: 'live', fetchImpl: async () => ({ ok: false, status: 404, json: async () => { throw new SyntaxError('HTML'); } }) });
  await assert.rejects(nonJson.listItems(), hasCode('BAD_RESPONSE'));
  const offline = createApi({ mode: 'live', fetchImpl: async () => { throw new TypeError('Failed to fetch'); } });
  await assert.rejects(offline.listItems(), hasCode('NETWORK'));
  await assert.rejects(offline.setBuzzer(true), hasCode('NETWORK'));
});

for (const phase of ['fetch', 'body']) test(`${phase} 대기 중 제한 시간이 지나면 취소하고 TIMEOUT을 알린다`, async () => {
  let aborted = false;
  const api = createApi({ mode: 'live', timeoutMs: 10, fetchImpl: async (_url, { signal }) => {
    const pending = () => new Promise((_resolve, reject) => signal.addEventListener('abort', () => {
      aborted = true; reject(new DOMException('Aborted', 'AbortError'));
    }, { once: true }));
    return phase === 'fetch' ? pending() : { ok: true, status: 200, json: pending };
  } });
  await assert.rejects(api.setBuzzer(false), hasCode('TIMEOUT'));
  assert.equal(aborted, true);
});

test('사진은 같은 서버 경로로 인코딩하고 샘플은 로컬 그림으로 연결한다', () => {
  const { api } = liveWith({});
  assert.equal(api.snapshotUrl(record()), 'http://jetson.local:8080/snapshots/key.jpg');
  assert.equal(api.snapshotUrl({ ...record(), snapshot: 'snapshots/차키 사진.jpg' }), 'http://jetson.local:8080/snapshots/%EC%B0%A8%ED%82%A4%20%EC%82%AC%EC%A7%84.jpg');
  assert.equal(api.snapshotUrl({ ...record(), snapshot: null }), null);
  assert.throws(() => api.snapshotUrl({ ...record(), snapshot: '/snapshots/key.jpg' }), hasCode('BAD_RESPONSE'));
  assert.equal(createApi().snapshotUrl(record()), './assets/demo-scene.svg');
});

test('모드·서버 주소·제한 시간 설정 오류를 알린다', () => {
  for (const options of [{ mode: 'mock' }, { apiBaseUrl: 'ftp://jetson' }, { timeoutMs: 0 }, { timeoutMs: Infinity }]) {
    assert.throws(() => createApi(options), hasCode('INVALID_CONFIG'));
  }
});
