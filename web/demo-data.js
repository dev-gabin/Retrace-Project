// 프로토콜 LastSeen 필드만 사용합니다. 샘플이 실제 관측으로 표시되지 않게 합니다.
const now = Date.now();
export const demoItems = [
  { item: 'carkey', pos_x: 412, pos_y: 288, seen_at: new Date(now - 7 * 60000).toISOString(), snapshot: 'snapshots/demo-carkey.svg', drawer_id: null, state: 'visible' },
  { item: 'airpods', pos_x: null, pos_y: null, seen_at: new Date(now - 18 * 60000).toISOString(), snapshot: null, drawer_id: 3, state: 'occluded' },
  { item: 'wallet', pos_x: 172, pos_y: 224, seen_at: new Date(now - 42 * 60000).toISOString(), snapshot: 'snapshots/demo-wallet.svg', drawer_id: null, state: 'uncertain' },
];

export function createDemoTransport() {
  const items = structuredClone(demoItems);
  let leaveItems = ['carkey'];
  return {
    async listItems() { return { items: structuredClone(items) }; },
    async getItem(item) { return structuredClone(items.find(record => record.item === item)); },
    async createDrawerItem(name, n) {
      const existing = items.find(record => record.item.startsWith('custom_') && record.display_name === name);
      const item = existing?.item ?? 'custom_' + crypto.randomUUID().replaceAll('-', '').slice(0, 24);
      await this.storeDrawer(item, n);
      items.find(record => record.item === item).display_name = name;
      return { ok: true, item, display_name: name };
    },
    async storeDrawer(item, n) {
      let record = items.find(record => record.item === item);
      if (!record) { record = { item }; items.push(record); }
      Object.assign(record, { drawer_id: n, pos_x: null, pos_y: null, snapshot: null, seen_at: new Date().toISOString(), state: 'occluded' });
      return { ok: true };
    },
    async openDrawer(n) {
      for (const record of items.filter(record => record.drawer_id === n)) {
        Object.assign(record, { drawer_id: null, pos_x: null, pos_y: null, snapshot: null, seen_at: null, state: 'uncertain' });
      }
      return { ok: true };
    },
    async aim() { return { ok: true }; },
    async getLeaveItems() { return { items: [...leaveItems] }; },
    async setLeaveItems(items) { leaveItems = [...new Set(items)]; return { ok: true, items: [...leaveItems] }; },
  };
}
