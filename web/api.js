import { createDemoTransport } from './demo-data.js';

export const ITEM_IDS = Object.freeze(['carkey', 'airpods', 'wallet']);
const states = ['visible', 'occluded', 'uncertain'];

export class ApiError extends Error {
  constructor(code, message, status = 0) {
    super(message);
    this.name = 'ApiError';
    this.code = code;
    this.status = status;
  }
}

function validItemId(item) {
  if (!ITEM_IDS.includes(item)) throw new ApiError('INVALID_ITEM', '지원하는 물건을 선택해 주세요.');
  return item;
}

function snapshotPath(path) {
  if (typeof path !== 'string' || !path.startsWith('snapshots/')) return false;
  const parts = path.split('/');
  return parts.length > 1 && parts.every(part => part && part !== '.' && part !== '..' && !/[\\?#\u0000-\u001f]/.test(part));
}

export function validateItem(record) {
  const bad = () => { throw new ApiError('BAD_RESPONSE', '서버의 물건 기록 형식이 프로토콜과 다릅니다.'); };
  if (!record || typeof record !== 'object' || Array.isArray(record) || !ITEM_IDS.includes(record.item)) bad();
  const xyEmpty = record.pos_x === null && record.pos_y === null;
  const xyValid = Number.isInteger(record.pos_x) && record.pos_x >= 0 && Number.isInteger(record.pos_y) && record.pos_y >= 0;
  if (!xyEmpty && !xyValid) bad();
  if (record.seen_at !== null && (typeof record.seen_at !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(record.seen_at) || !Number.isFinite(Date.parse(record.seen_at)))) bad();
  if (record.snapshot !== null && !snapshotPath(record.snapshot)) bad();
  if (record.drawer_id !== null && (!Number.isInteger(record.drawer_id) || record.drawer_id < 1 || record.drawer_id > 6)) bad();
  if (!states.includes(record.state)) bad();
  if (record.seen_at === null && (!xyEmpty || record.snapshot !== null || record.drawer_id !== null)) bad();
  return { item: record.item, pos_x: record.pos_x, pos_y: record.pos_y, seen_at: record.seen_at, snapshot: record.snapshot, drawer_id: record.drawer_id, state: record.state };
}

function acknowledged(result) {
  if (!result || result.ok !== true) throw new ApiError('BAD_RESPONSE', '서버의 요청 접수 응답을 확인할 수 없습니다.');
  return result;
}

export function createApi({ mode = 'demo', apiBaseUrl = '', timeoutMs = 8000, fetchImpl = globalThis.fetch } = {}) {
  if (!['demo', 'live'].includes(mode)) throw new ApiError('INVALID_CONFIG', 'API 모드 설정을 확인해 주세요.');
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new ApiError('INVALID_CONFIG', '요청 제한 시간을 확인해 주세요.');
  if (apiBaseUrl !== '' && !/^https?:\/\//i.test(apiBaseUrl)) throw new ApiError('INVALID_CONFIG', '서버 주소는 HTTP 또는 HTTPS 주소여야 합니다.');
  const base = apiBaseUrl.replace(/\/+$/, '');
  const demo = createDemoTransport();

  async function request(path, { method = 'GET', body } = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const headers = { Accept: 'application/json' };
      const options = { method, headers, signal: controller.signal, cache: 'no-store' };
      if (body !== undefined) { headers['Content-Type'] = 'application/json'; options.body = JSON.stringify(body); }
      const response = await fetchImpl(`${base}${path}`, options);
      let data;
      try { data = await response.json(); } catch (error) {
        if (controller.signal.aborted) throw error;
        throw new ApiError('BAD_RESPONSE', '서버가 JSON 응답을 보내지 않았습니다.', response.status);
      }
      if (!response.ok) {
        throw new ApiError(typeof data?.error?.code === 'string' ? data.error.code : `HTTP_${response.status}`, typeof data?.error?.message === 'string' ? data.error.message.slice(0, 220) : '서버에서 요청을 처리하지 못했습니다.', response.status);
      }
      return data;
    } catch (error) {
      if (error instanceof ApiError) throw error;
      if (controller.signal.aborted) throw new ApiError('TIMEOUT', '응답 시간이 초과됐습니다. 기기 동작 여부는 확인되지 않았어요.');
      throw new ApiError('NETWORK', '서버에 연결하지 못했습니다. 연결 상태를 확인해 주세요.');
    } finally { clearTimeout(timer); }
  }

  return Object.freeze({
    mode,
    async listItems() {
      const data = mode === 'demo' ? await demo.listItems() : await request('/api/items');
      if (!Array.isArray(data?.items)) throw new ApiError('BAD_RESPONSE', '서버의 물건 목록 형식이 프로토콜과 다릅니다.');
      const items = data.items.map(validateItem);
      if (new Set(items.map(record => record.item)).size !== items.length) throw new ApiError('BAD_RESPONSE', '서버 목록에 물건 ID가 중복되어 있습니다.');
      return items;
    },
    async getItem(item) {
      validItemId(item);
      const record = validateItem(mode === 'demo' ? await demo.getItem(item) : await request(`/api/items/${encodeURIComponent(item)}`));
      if (record.item !== item) throw new ApiError('BAD_RESPONSE', '요청한 물건과 상세 기록이 다릅니다.');
      return record;
    },
    async aim(item) {
      validItemId(item);
      return acknowledged(mode === 'demo' ? await demo.aim(item) : await request(`/api/items/${encodeURIComponent(item)}/aim`, { method: 'POST', body: {} }));
    },
    async openDrawer(n) {
      if (!Number.isInteger(n) || n < 1 || n > 6) throw new ApiError('INVALID_DRAWER', '서랍 번호는 1~6이어야 합니다.');
      return acknowledged(mode === 'demo' ? await demo.openDrawer(n) : await request(`/api/drawers/${n}/open`, { method: 'POST', body: {} }));
    },
    async setBuzzer(enabled) {
      if (typeof enabled !== 'boolean') throw new ApiError('INVALID_BUZZER', '부저 요청은 켜기 또는 끄기로 정해야 합니다.');
      return acknowledged(mode === 'demo' ? await demo.setBuzzer(enabled) : await request('/api/buzzer', { method: 'POST', body: { enabled } }));
    },
    snapshotUrl(record) {
      if (record.snapshot === null) return null;
      if (!snapshotPath(record.snapshot)) throw new ApiError('BAD_RESPONSE', '사진 경로가 올바르지 않습니다.');
      return mode === 'demo' ? './assets/demo-scene.svg' : `${base}/${record.snapshot.split('/').map(encodeURIComponent).join('/')}`;
    },
  });
}
