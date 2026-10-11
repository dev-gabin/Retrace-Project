import { config } from './config.js';
import { createApi, ApiError } from './api.js';

const api = createApi(config);
const isDemo = api.mode === 'demo';
const labels = { car_key: '차키', carkey: '차키', airpods: '에어팟', earphones: '이어폰', wallet: '지갑' };
const icons = { car_key: 'key', carkey: 'key', airpods: 'airpods', earphones: 'airpods', wallet: 'wallet' };
const descriptions = { car_key: '외출할 때 함께', carkey: '외출할 때 함께', airpods: '나만의 작은 음악', earphones: '나만의 작은 음악', wallet: '외출할 때 챙기는 지갑' };
const stateLabels = { visible: '마지막 화면에서 확인', occluded: '가려진 상태로 기록', uncertain: '위치 확인 필요' };
// Leave checks are for things a person may carry out, not every class YOLO can detect.
const leaveItemChoices = [
  'car_key', 'wallet', 'earphones', 'cell_phone',
  'backpack', 'umbrella', 'handbag', 'suitcase',
  'laptop', 'mouse', 'remote', 'keyboard', 'book',
  'bottle', 'cup',
];
// Keep the default drawer choices to small objects; custom names remain available.
const drawerItemChoices = [
  'car_key', 'wallet', 'earphones', 'cell_phone', 'mouse', 'remote',
];
const state = { items: [], selected: null, record: null, drawer: 1, detailLoading: false, detailSequence: 0, busy: new Set(), activity: [], leaveItems: new Set() };
const $ = id => document.getElementById(id);
let toastTimer;
let activeSpeech = null;

function itemLabel(item) {
  return state.items.find(record => record.item === item)?.display_name ?? labels[item] ?? item.replaceAll('_', ' ');
}

function itemIcon(item) {
  return icons[item] ?? 'image';
}

function itemDescription(item) {
  if (state.items.find(record => record.item === item)?.display_name) return '직접 등록한 보관 물건';
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
  if (record.drawer_id !== null) return `${record.drawer_id}번 서랍에 보관 등록했어요`;
  return record.pos_x === null ? '위치 정보 없음' : '카메라 감지 영역에 기록되어 있어요';
}

function spokenTime(value, now = new Date()) {
  if (!value) return '';
  const date = new Date(value);
  if (!Number.isFinite(date.getTime())) return '';
  const formatter = new Intl.DateTimeFormat('ko-KR', { year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', hour12: true, timeZone: 'Asia/Seoul' });
  const parts = value => Object.fromEntries(formatter.formatToParts(value).map(part => [part.type, part.value]));
  const time = parts(date);
  const dayKey = value => `${value.year}-${value.month}-${value.day}`;
  const day = dayKey(time) === dayKey(parts(now)) ? '오늘' : dayKey(time) === dayKey(parts(new Date(now.getTime() - 86400000))) ? '어제' : `${time.year}년 ${time.month}월 ${time.day}일`;
  return `${day} ${time.dayPeriod} ${Number(time.hour)}시${Number(time.minute) ? ` ${Number(time.minute)}분` : ''}쯤`;
}

function spokenDrawerNumber(n) {
  return ['', '일 번', '이 번', '삼 번', '사 번', '오 번', '육 번'][n] ?? `${n}번`;
}

function spokenRecord(record, now = new Date()) {
  if (!record || record.drawer_id === null) return '';
  const when = spokenTime(record?.seen_at, now);
  if (!when) return '';
  const name = itemLabel(record.item);
  const last = name.charCodeAt(name.length - 1);
  const topic = last >= 0xac00 && last <= 0xd7a3 && (last - 0xac00) % 28 !== 0 ? '은' : '는';
  const location = `${spokenDrawerNumber(record.drawer_id)} 서랍에 보관 등록했어요.`;
  return `${isDemo ? '샘플 기록입니다. ' : ''}${name}${topic} ${when} ${location}`;
}

function speechAvailable() {
  return Boolean(globalThis.speechSynthesis && typeof globalThis.SpeechSynthesisUtterance === 'function');
}

function stopRecordSpeech() {
  if (!activeSpeech) return;
  activeSpeech = null;
  globalThis.speechSynthesis.cancel();
  updateButtons();
}

function readRecord(record, { automatic = false } = {}) {
  speakText(spokenRecord(record), { automatic });
}

function readDrawerRegistration(name, n, seenAt) {
  const last = name.charCodeAt(name.length - 1);
  const subject = last >= 0xac00 && last <= 0xd7a3 && (last - 0xac00) % 28 !== 0 ? '이' : '가';
  const when = spokenTime(seenAt);
  const text = `${isDemo ? '샘플 등록입니다. ' : ''}${name}${subject} ${when ? `${when} ` : ''}${spokenDrawerNumber(n)} 서랍에 등록됐어요.`;
  try { speakText(text, { automatic: true }); }
  catch { showToast('보관 등록은 완료됐지만 음성을 재생하지 못했어요.', true); }
}

function speakText(text, { automatic = false } = {}) {
  if (!text) return;
  if (!speechAvailable()) {
    if (!automatic) showToast('이 브라우저에서는 음성 읽어주기를 사용할 수 없어요.', true);
    return;
  }
  stopRecordSpeech();
  const utterance = new globalThis.SpeechSynthesisUtterance(text);
  utterance.lang = 'ko-KR';
  utterance.rate = 1;
  const voice = globalThis.speechSynthesis.getVoices().find(entry => /^ko(?:-|$)/i.test(entry.lang));
  if (voice) utterance.voice = voice;
  const finish = () => {
    if (activeSpeech !== utterance) return false;
    activeSpeech = null;
    updateButtons();
    return true;
  };
  utterance.onend = finish;
  utterance.onerror = event => {
    if (finish() && !['canceled', 'interrupted'].includes(event.error)) showToast('음성을 재생하지 못했어요. 읽어주기 버튼을 눌러 다시 시도해 주세요.', true);
  };
  activeSpeech = utterance;
  updateButtons();
  try { globalThis.speechSynthesis.speak(utterance); }
  catch { utterance.onerror({ error: 'synthesis-failed' }); }
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

function leaveItemLabel(item) {
  const label = itemLabel(item);
  return label === item.replaceAll('_', ' ') ? item : `${label} (${item})`;
}

function renderLeaveRegistration() {
  const select = $('leave-item-select');
  const current = select.value;
  const fragment = document.createDocumentFragment();
  for (const item of leaveItemChoices) {
    const option = document.createElement('option');
    option.value = item;
    option.textContent = leaveItemLabel(item);
    option.disabled = state.leaveItems.has(item);
    fragment.append(option);
  }
  select.replaceChildren(fragment);
  select.value = leaveItemChoices.includes(current) && !state.leaveItems.has(current) ? current : leaveItemChoices.find(item => !state.leaveItems.has(item)) ?? '';
  $('leave-item-add-button').disabled = !select.value || state.busy.has('leave');

  const registered = [...state.leaveItems].sort((a, b) => leaveItemChoices.indexOf(a) - leaveItemChoices.indexOf(b));
  const list = $('leave-item-list');
  if (!registered.length) {
    list.textContent = '등록된 외출 확인 물건이 없어요.';
    return;
  }
  const chips = document.createDocumentFragment();
  for (const item of registered) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'leave-item-chip';
    button.disabled = state.busy.has('leave');
    button.textContent = `${leaveItemLabel(item)} ×`;
    button.setAttribute('aria-label', `${leaveItemLabel(item)} 외출 확인 해제`);
    button.addEventListener('click', () => setLeaveItems([...state.leaveItems].filter(entry => entry !== item), `${itemLabel(item)} 외출 알림 해제`));
    chips.append(button);
  }
  list.replaceChildren(chips);
}

function setLeaveItems(items, description) {
  void command('leave', 'key', description, async () => {
    state.leaveItems = new Set(await api.setLeaveItems(items));
  }, () => { renderItems(); updateButtons(); }).finally(() => {
    renderLeaveRegistration();
    renderItems();
    updateButtons();
  });
  renderLeaveRegistration();
}

function renderDrawerItems() {
  const select = $('drawer-item-select');
  const current = select.value;
  const fragment = document.createDocumentFragment();
  const customItems = state.items.filter(record => record.display_name).map(record => record.item);
  for (const item of [...new Set([...drawerItemChoices, ...customItems])]) {
    const option = document.createElement('option');
    option.value = item;
    option.textContent = customItems.includes(item) ? itemLabel(item) : leaveItemLabel(item);
    fragment.append(option);
  }
  select.replaceChildren(fragment);
  if ([...select.options].some(option => option.value === current)) select.value = current;
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
    for (const [className, text] of [['item-name', itemLabel(record.item)], ['item-description', itemDescription(record.item)], ['item-time', record.drawer_id !== null ? `${record.drawer_id}번 서랍 보관 중` : relativeTime(record.seen_at)]]) {
      const span = document.createElement('span');
      span.className = className;
      span.textContent = text;
      button.append(span);
    }
    if (state.leaveItems.has(record.item)) {
      const badge = document.createElement('span');
      badge.className = 'item-badge';
      badge.textContent = '외출 알림';
      button.append(badge);
    }
    button.addEventListener('click', () => selectItem(record.item, { navigateToDrawer: true, readAloud: record.drawer_id !== null }));
    fragment.append(button);
  }
  $('item-list').replaceChildren(fragment);
  if (focusedItem) $('item-list').querySelector(`[data-item="${focusedItem}"]`)?.focus();
  $('list-message').textContent = state.items.length === 0 ? '아직 등록된 물건이 없어요.' : matches.length === 0 ? '검색한 물건이 없어요. 다른 이름으로 찾아보세요.' : '';
}

function updateButtons() {
  const record = state.record;
  $('aim-button').disabled = state.detailLoading || !record || record.seen_at === null || record.pos_x === null || record.drawer_id !== null || state.busy.has('aim');
  $('record-speech-button').hidden = !record || record.drawer_id === null;
  $('record-speech-button').disabled = state.detailLoading || !record?.seen_at || record.drawer_id === null || !speechAvailable();
  $('record-speech-button').querySelector('span').textContent = activeSpeech ? '읽기 중지' : '보관 시각 읽어주기';
  $('record-speech-button').title = speechAvailable() ? '선택한 서랍 물건의 보관 등록 시각을 읽어요' : '이 브라우저에서는 음성 읽어주기를 지원하지 않아요';
  $('drawer-open-button').disabled = state.busy.has('drawer');
  const storageItem = $('drawer-item-select').value;
  $('drawer-item-select').disabled = state.busy.has('drawer');
  $('drawer-custom-name').disabled = state.busy.has('drawer');
  $('drawer-custom-add-button').disabled = !$('drawer-custom-name').value.trim() || state.busy.has('drawer');
  $('drawer-store-button').disabled = !storageItem || state.busy.has('drawer');
  const storageLabel = storageItem ? `${itemLabel(storageItem)}을 ${state.drawer}번 서랍에 보관 등록` : '보관할 물건을 선택해 주세요';
  $('drawer-store-button').setAttribute('aria-label', storageLabel);
  $('drawer-store-button').title = storageLabel;
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
  $('record-time-label').textContent = record && record.drawer_id !== null ? '보관 등록 시각' : '마지막 목격';
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
  $('record-state').textContent = record.drawer_id !== null ? '서랍 보관 등록' : record.seen_at ? stateLabels[record.state] : '기록 없음';
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

async function selectItem(item, { navigateToDrawer = false, readAloud = false } = {}) {
  stopRecordSpeech();
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
    if (sequence === state.detailSequence) {
      state.detailLoading = false;
      renderDetail();
      if (navigateToDrawer && state.record && state.record.drawer_id !== null) showItemDrawer(state.record);
      if (readAloud && state.record && state.record.drawer_id !== null) readRecord(state.record, { automatic: true });
    }
  }
}

async function refreshItems() {
  stopRecordSpeech();
  $('refresh-button').disabled = true;
  $('refresh-button').classList.add('is-loading');
  $('item-list').setAttribute('aria-busy', 'true');
  try {
    const [items, leaveItems] = await Promise.all([api.listItems(), api.getLeaveItems().catch(() => null)]);
    state.items = items;
    renderDrawerItems();
    if (leaveItems) state.leaveItems = new Set(leaveItems);
    renderLeaveRegistration();
    showError();
    if (items.length) await selectItem(items.some(record => record.item === state.selected) ? state.selected : items[0].item);
    else { ++state.detailSequence; state.selected = null; state.record = null; state.detailLoading = false; renderItems(); renderDetail(); }
    return items;
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
  updateButtons();
}

function showItemDrawer(record) {
  selectDrawer(record.drawer_id);
  if ([...$('drawer-item-select').options].some(option => option.value === record.item)) $('drawer-item-select').value = record.item;
  updateButtons();
  $('drawer-open-button').focus({ preventScroll: true });
  $('drawer').scrollIntoView({ behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'instant' : 'smooth', block: 'center' });
}

$('search-input').addEventListener('input', renderItems);
$('refresh-button').addEventListener('click', refreshItems);
$('record-speech-button').addEventListener('click', () => {
  if ($('record-speech-button').disabled || !state.record) return;
  if (activeSpeech) stopRecordSpeech();
  else readRecord(state.record);
});
globalThis.addEventListener('pagehide', stopRecordSpeech);
$('aim-button').addEventListener('click', () => {
  if ($('aim-button').disabled || !state.record) return;
  const item = state.record.item;
  command('aim', 'target', `${itemLabel(item)} 레이저 안내`, () => api.aim(item));
});
document.querySelectorAll('[data-drawer]').forEach(button => button.addEventListener('click', () => selectDrawer(Number(button.dataset.drawer))));
$('drawer-open-button').addEventListener('click', () => {
  const n = state.drawer;
  command('drawer', 'drawer', `${n}번 서랍 열기`, async () => { await api.openDrawer(n); await refreshItems(); });
});
$('drawer-store-button').addEventListener('click', () => {
  if ($('drawer-store-button').disabled) return;
  const item = $('drawer-item-select').value;
  const n = state.drawer;
  const name = itemLabel(item);
  let registeredAt;
  command('drawer', 'drawer', `${name} ${n}번 서랍 보관 등록`, async () => {
    await api.storeDrawer(item, n);
    const items = await refreshItems();
    registeredAt = items?.find(record => record.item === item && record.drawer_id === n)?.seen_at;
  }, () => readDrawerRegistration(name, n, registeredAt));
});
$('drawer-item-select').addEventListener('change', updateButtons);
$('drawer-custom-name').addEventListener('input', updateButtons);
$('drawer-custom-add-button').addEventListener('click', () => {
  if ($('drawer-custom-add-button').disabled) return;
  const name = $('drawer-custom-name').value.trim();
  const n = state.drawer;
  let registeredAt;
  command('drawer', 'drawer', `${name} ${n}번 서랍 추가·보관`, async () => {
    const result = await api.createDrawerItem(name, n);
    $('drawer-custom-name').value = '';
    const items = await refreshItems();
    registeredAt = items?.find(record => record.item === result.item && record.drawer_id === n)?.seen_at;
    if (state.items.some(record => record.item === result.item)) $('drawer-item-select').value = result.item;
  }, () => readDrawerRegistration(name, n, registeredAt));
});
$('drawer-custom-name').addEventListener('keydown', event => {
  if (event.key === 'Enter' && !event.isComposing) { event.preventDefault(); $('drawer-custom-add-button').click(); }
});
$('leave-item-add-button').addEventListener('click', () => {
  const item = $('leave-item-select').value;
  if (!item || state.leaveItems.has(item)) return;
  setLeaveItems([...state.leaveItems, item], `${itemLabel(item)} 외출 알림 등록`);
});
$('related-drawer-button').addEventListener('click', () => {
  if (state.record?.drawer_id === null || !state.record) return;
  showItemDrawer(state.record);
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
renderDrawerItems();
renderLeaveRegistration();
refreshItems();
