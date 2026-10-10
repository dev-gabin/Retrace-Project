import { config } from './config.js';
import { createApi, ApiError } from './api.js';

const api = createApi(config);
const isDemo = api.mode === 'demo';
const labels = { car_key: '차키', carkey: '차키', airpods: '에어팟', earphones: '이어폰', wallet: '지갑' };
const icons = { car_key: 'key', carkey: 'key', airpods: 'airpods', earphones: 'airpods', wallet: 'wallet' };
const descriptions = { car_key: '외출할 때 함께', carkey: '외출할 때 함께', airpods: '나만의 작은 음악', earphones: '나만의 작은 음악', wallet: '외출할 때 챙기는 지갑' };
const stateLabels = { visible: '마지막 화면에서 확인', occluded: '가려진 상태로 기록', uncertain: '위치 확인 필요' };
const state = { items: [], selected: null, record: null, drawer: 1, detailLoading: false, detailSequence: 0, busy: new Set(), activity: [] };
const $ = id => document.getElementById(id);
let toastTimer;

function itemLabel(item) {
  return labels[item] ?? item.replaceAll('_', ' ');
}

function itemIcon(item) {
  return icons[item] ?? 'image';
}

function itemDescription(item) {
  return descriptions[item] ?? '마지막으로 확인된 물건';
}

function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.classList.add('icon');
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(svg.namespaceURI, 'use');
  use.setAttribute('href', `#icon-${name}`);
  svg.append(use);
  return svg;
}

function relativeTime(value) {
  if (!value) return '기록 없음';
  const minutes = Math.max(0, Math.floor((Date.now() - Date.parse(value)) / 60000));
  if (minutes < 1) return '방금 전';
  if (minutes < 60) return `${minutes}분 전`;
  if (minutes < 1440) return `${Math.floor(minutes / 60)}시간 전`;
  return `${Math.floor(minutes / 1440)}일 전`;
}

function locationText(record) {
  if (!record.seen_at) return '아직 목격 기록이 없어요';
  if (record.drawer_id !== null) return `${record.drawer_id}번 서랍에 기록되어 있어요`;
  return record.pos_x === null ? '위치 정보 없음' : '카메라 감지 영역에 기록되어 있어요';
}

function showToast(message, error = false) {
  clearTimeout(toastTimer);
  $('toast').textContent = message;
  $('toast').classList.toggle('is-error', error);
  $('toast').hidden = false;
  toastTimer = setTimeout(() => { $('toast').hidden = true; }, error ? 7000 : 4500);
}

function showError(message = '') {
  $('load-error').textContent = message;
  $('load-error').hidden = !message;
}

function renderItems() {
  const focusedItem = $('item-list').contains(document.activeElement) ? document.activeElement.dataset.item : null;
  const query = $('search-input').value.trim().toLocaleLowerCase('ko-KR');
  const matches = state.items.filter(record => itemLabel(record.item).toLocaleLowerCase('ko-KR').includes(query) || record.item.includes(query));
  $('item-count').textContent = state.items.length;
  const fragment = document.createDocumentFragment();
  for (const record of matches) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `item-card${record.item === state.selected ? ' is-selected' : ''}`;
    button.dataset.item = record.item;
    button.setAttribute('aria-pressed', String(record.item === state.selected));
    button.setAttribute('aria-label', `${itemLabel(record.item)} 상세 보기`);
    const holder = document.createElement('span');
    holder.className = 'item-icon';
    holder.append(icon(itemIcon(record.item)));
    button.append(holder);
    for (const [className, text] of [['item-name', itemLabel(record.item)], ['item-description', itemDescription(record.item)], ['item-time', relativeTime(record.seen_at)]]) {
      const span = document.createElement('span');
      span.className = className;
      span.textContent = text;
      button.append(span);
    }
    button.addEventListener('click', () => selectItem(record.item));
    fragment.append(button);
  }
  $('item-list').replaceChildren(fragment);
  if (focusedItem) $('item-list').querySelector(`[data-item="${focusedItem}"]`)?.focus();
  $('list-message').textContent = state.items.length === 0 ? '아직 등록된 물건이 없어요.' : matches.length === 0 ? '검색한 물건이 없어요. 다른 이름으로 찾아보세요.' : '';
}

function updateButtons() {
  const record = state.record;
  $('aim-button').disabled = state.detailLoading || !record || record.seen_at === null || record.pos_x === null || record.drawer_id !== null || state.busy.has('aim');
  $('drawer-open-button').disabled = state.busy.has('drawer');
  $('buzzer-on-button').disabled = state.busy.has('buzzer');
  $('buzzer-off-button').disabled = state.busy.has('buzzer');
}

function clearPhoto(title) {
  $('snapshot-image').hidden = true;
  $('snapshot-image').removeAttribute('src');
  $('photo-empty').hidden = false;
  $('photo-empty-title').textContent = title;
  $('scene-target').hidden = true;
  $('scene-label').hidden = true;
}

function renderDetail() {
  const record = state.record;
  $('detail-card').setAttribute('aria-busy', String(state.detailLoading));
  if (!record) {
    clearPhoto(state.detailLoading ? '마지막 기록을 불러오는 중이에요' : '표시할 기록이 없어요');
    $('selected-name').textContent = state.selected ? itemLabel(state.selected) : '물건을 선택해 주세요';
    $('selected-location').textContent = '마지막 기록을 확인할 수 있어요.';
    $('seen-at').textContent = '—';
    $('record-location').textContent = '—';
    $('relative-time').textContent = '—';
    $('record-state').textContent = state.detailLoading ? '기록 확인 중' : '기록 없음';
    $('record-state').className = 'record-state';
    $('related-drawer-button').hidden = true;
    $('aim-help').textContent = '선택한 물건의 마지막 위치로 안내를 요청해요.';
    updateButtons();
    return;
  }
  $('selected-name').textContent = itemLabel(record.item);
  $('selected-location').textContent = locationText(record);
  $('relative-time').textContent = relativeTime(record.seen_at);
  $('seen-at').textContent = record.seen_at ? new Intl.DateTimeFormat('ko-KR', { month: 'long', day: 'numeric', hour: '2-digit', minute: '2-digit', timeZone: 'Asia/Seoul' }).format(new Date(record.seen_at)) : '기록 없음';
  $('record-location').textContent = record.drawer_id !== null ? `${record.drawer_id}번 서랍` : record.pos_x !== null ? `이미지 좌표 (${record.pos_x}, ${record.pos_y})` : '위치 정보 없음';
  $('record-state').textContent = record.seen_at ? stateLabels[record.state] : '기록 없음';
  $('record-state').className = `record-state ${record.state}`;
  $('related-drawer-button').hidden = record.drawer_id === null;
  $('related-drawer-button').textContent = `${record.drawer_id}번 서랍 선택하기`;
  $('aim-help').textContent = record.drawer_id !== null ? '서랍 안에 기록된 물건은 서랍을 열어 확인해 주세요.' : record.pos_x === null || record.seen_at === null ? '위치 기록이 있어야 안내를 요청할 수 있어요.' : '목격 이후 물건이 이동했다면 위치가 달라질 수 있어요.';
  const src = api.snapshotUrl(record);
  if (src) {
    $('photo-empty').hidden = true;
    $('snapshot-image').alt = `${itemLabel(record.item)}의 ${isDemo ? '샘플' : '마지막 목격'} 장면`;
    $('snapshot-image').src = src;
    $('snapshot-image').hidden = false;
    $('scene-label').textContent = isDemo ? '샘플 장면 · 실제 촬영 사진 아님' : '마지막 목격 사진';
    $('scene-label').hidden = false;
    // 임시 샘플 이미지(640×340)에만 좌표를 올립니다. 실제 이미지 크기는 서버와 합의 전입니다.
    $('scene-target').hidden = !isDemo || record.pos_x === null;
    if (isDemo && record.pos_x !== null) {
      $('scene-target').style.left = `${Math.min(92, Math.max(8, record.pos_x / 640 * 100))}%`;
      $('scene-target').style.top = `${Math.min(92, Math.max(20, record.pos_y / 340 * 100))}%`;
      $('target-label').textContent = itemLabel(record.item);
    }
  } else { clearPhoto('저장된 사진이 없어요'); }
  updateButtons();
}

async function selectItem(item) {
  const sequence = ++state.detailSequence;
  state.selected = item;
  state.record = null;
  state.detailLoading = true;
  renderItems();
  renderDetail();
  try {
    const record = await api.getItem(item);
    if (sequence !== state.detailSequence) return;
    state.record = record;
    showError();
  } catch (error) {
    if (sequence !== state.detailSequence) return;
    showError(`상세 기록을 불러오지 못했어요. ${error.message}`);
  } finally {
    if (sequence === state.detailSequence) { state.detailLoading = false; renderDetail(); }
  }
}

async function refreshItems() {
  $('refresh-button').disabled = true;
  $('refresh-button').classList.add('is-loading');
  $('item-list').setAttribute('aria-busy', 'true');
  try {
    const items = await api.listItems();
    state.items = items;
    showError();
    if (items.length) await selectItem(items.some(record => record.item === state.selected) ? state.selected : items[0].item);
    else { ++state.detailSequence; state.selected = null; state.record = null; state.detailLoading = false; renderItems(); renderDetail(); }
  } catch (error) {
    showError(`기록을 새로 불러오지 못했어요. ${error.message}${state.items.length ? ' 이전에 조회한 기록을 표시하고 있어요.' : ''}`);
    if (!state.items.length) {
      state.detailLoading = false; renderItems(); renderDetail();
      $('list-message').textContent = '목록을 불러오지 못했어요. 연결 상태를 확인한 후 새로고침해 주세요.';
    }
  } finally {
    $('refresh-button').disabled = false;
    $('refresh-button').classList.remove('is-loading');
    $('item-list').setAttribute('aria-busy', 'false');
  }
}

function addActivity(message, kind, failed) {
  state.activity.unshift({ message, kind, failed, date: new Date() });
  state.activity = state.activity.slice(0, 5);
  const rows = state.activity.map(entry => {
    const li = document.createElement('li');
    li.classList.toggle('activity-error', entry.failed);
    li.append(icon(entry.kind));
    const text = document.createElement('span');
    text.textContent = `${isDemo ? '샘플 · ' : ''}${entry.message}`;
    const time = document.createElement('time');
    time.className = 'activity-time';
    time.dateTime = entry.date.toISOString();
    time.textContent = entry.date.toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'Asia/Seoul' });
    li.append(text, time);
    return li;
  });
  $('activity-list').replaceChildren(...rows);
}

async function command(key, kind, description, send, onAccepted) {
  if (state.busy.has(key)) return;
  state.busy.add(key);
  updateButtons();
  try {
    await send();
    const message = `${description} 요청이 ${isDemo ? '샘플로 ' : ''}접수됐어요.`;
    addActivity(`${description} 요청 접수`, kind, false);
    showToast(message);
    onAccepted?.();
  } catch (error) {
    addActivity(`${description} 요청 실패`, kind, true);
    showToast(error instanceof ApiError ? error.message : '요청을 처리하지 못했습니다.', true);
  } finally { state.busy.delete(key); updateButtons(); }
}

function selectDrawer(n) {
  state.drawer = n;
  document.querySelectorAll('[data-drawer]').forEach(button => {
    const selected = Number(button.dataset.drawer) === n;
    button.classList.toggle('is-selected', selected);
    button.setAttribute('aria-pressed', String(selected));
  });
  $('drawer-open-button').querySelector('span').textContent = `${n}번 서랍 열기`;
}

$('search-input').addEventListener('input', renderItems);
$('refresh-button').addEventListener('click', refreshItems);
$('aim-button').addEventListener('click', () => {
  if ($('aim-button').disabled || !state.record) return;
  const item = state.record.item;
  command('aim', 'target', `${itemLabel(item)} 레이저 안내`, () => api.aim(item));
});
document.querySelectorAll('[data-drawer]').forEach(button => button.addEventListener('click', () => selectDrawer(Number(button.dataset.drawer))));
$('drawer-open-button').addEventListener('click', () => {
  const n = state.drawer;
  command('drawer', 'drawer', `${n}번 서랍 열기`, () => api.openDrawer(n));
});
$('related-drawer-button').addEventListener('click', () => {
  if (state.record?.drawer_id === null || !state.record) return;
  selectDrawer(state.record.drawer_id);
  $('drawer-open-button').focus();
  $('drawer').scrollIntoView({ behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'instant' : 'smooth', block: 'center' });
});
for (const [id, enabled] of [['buzzer-on-button', true], ['buzzer-off-button', false]]) {
  $(id).addEventListener('click', () => command('buzzer', 'sound', enabled ? '부저 찾기' : '부저 소리 끄기', () => api.setBuzzer(enabled), () => {
    $('buzzer-status').textContent = isDemo ? enabled ? '샘플 · 찾기 요청됨, 실제 소리 없음' : '샘플 · 소리 끄기 요청됨' : enabled ? '찾기 요청 접수 · 실제 소리 상태 미확인' : '끄기 요청 접수 · 실제 소리 상태 미확인';
    $('sound-bars').classList.toggle('is-active', isDemo && enabled);
  }));
}
$('snapshot-image').addEventListener('error', () => clearPhoto('사진을 불러오지 못했어요'));
if (!isDemo) {
  $('mode-chip').lastChild.textContent = '서버 모드';
  $('mode-notice-title').textContent = '서버에 연결하는 화면이에요.';
  $('mode-notice-copy').textContent = '버튼을 누르면 실제 제어 요청을 보냅니다. 기기 동작 상태는 별도로 확인해 주세요.';
  $('footer-mode').textContent = '서버 모드 · 요청 접수와 실제 동작은 별도';
  $('buzzer-status').textContent = '실제 소리 상태 미확인';
}
refreshItems();
