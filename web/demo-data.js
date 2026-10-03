// 프로토콜 LastSeen 필드만 사용합니다. 샘플이 실제 관측으로 표시되지 않게 합니다.
const now = Date.now();
export const demoItems = [
  { item: 'carkey', pos_x: 412, pos_y: 288, seen_at: new Date(now - 7 * 60000).toISOString(), snapshot: 'snapshots/demo-carkey.svg', drawer_id: null, state: 'visible' },
  { item: 'airpods', pos_x: null, pos_y: null, seen_at: new Date(now - 18 * 60000).toISOString(), snapshot: null, drawer_id: 3, state: 'occluded' },
  { item: 'glasses', pos_x: 172, pos_y: 224, seen_at: new Date(now - 42 * 60000).toISOString(), snapshot: 'snapshots/demo-glasses.svg', drawer_id: null, state: 'uncertain' },
];

export function createDemoTransport() {
  let buzzerEnabled = false;
  return {
    async listItems() { return { items: structuredClone(demoItems) }; },
    async getItem(item) { return structuredClone(demoItems.find(record => record.item === item)); },
    async aim() { return { ok: true }; },
    async openDrawer() { return { ok: true }; },
    async setBuzzer(enabled) { buzzerEnabled = enabled; return { ok: true }; },
    get buzzerEnabled() { return buzzerEnabled; },
  };
}
