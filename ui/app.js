'use strict';

const POLL_MS = 2000;
const STALE_MS = 10000;
const $ = (id) => document.getElementById(id);

let status = null;      // last good /api/status payload
let lastOkAt = 0;       // performance.now() of the last good poll
let period = 'today';
let opener = null;      // element to refocus when the dialog closes

const fmt = (n, d) => n.toLocaleString('en-US', { minimumFractionDigits: d, maximumFractionDigits: d });
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function parseIso(iso) {
  const p = String(iso || '').split('-').map(Number);
  return p.length === 3 && p[0] ? new Date(p[0], p[1] - 1, p[2]) : null;
}
function pretty(iso) {
  const d = parseIso(iso);
  return d ? MONTHS[d.getMonth()] + ' ' + d.getDate() + ', ' + d.getFullYear() : '—';
}
// "Today" is the device's local date when its clock is valid, else the phone's.
function daysSince(iso) {
  const d = parseIso(iso);
  if (!d) return 0;
  const now = status && status.timeValid ? parseIso(status.time.slice(0, 10)) : new Date();
  const t = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  return Math.max(0, Math.round((t - d) / 86400000));
}

// ---- TDS ----
const TDS_BANDS = [
  [50, 'Excellent', '#123A2B', '#7DEBB5'],
  [100, 'Good', '#27350F', '#C5E37C'],
  [170, 'Fair', '#3D2E10', '#F2C76E'],
  [Infinity, 'Poor', '#3E1A14', '#FF9A85'],
];

function renderTds(t) {
  $('tds-value').textContent = fmt(t.product, 0);
  $('tds-feed').textContent = fmt(t.feed, 0);
  $('tds-rej').textContent = fmt(t.rejection, 1);
  const band = TDS_BANDS.find((b) => t.product < b[0]);
  const pill = $('tds-pill');
  pill.textContent = band[1];
  pill.style.background = band[2];
  pill.style.color = band[3];
  $('tds-marker').style.left = Math.min(100, Math.max(0, (t.product / 200) * 100)) + '%';
}

// ---- Volume ----
function renderVolume() {
  if (!status) return;
  const v = status.volume[period];
  const total = v.clean + v.waste;
  const cleanPct = total > 0 ? (v.clean / total) * 100 : 0;
  $('v-clean').textContent = fmt(v.clean, 1);
  $('v-waste').textContent = fmt(v.waste, 1);
  $('v-total').textContent = fmt(total, 1);
  $('v-clean-pct').textContent = fmt(cleanPct, 0);
  $('v-waste-pct').textContent = fmt(total > 0 ? 100 - cleanPct : 0, 0);
  $('v-ratio').textContent = v.clean > 0 ? fmt(v.waste / v.clean, 1) : '0.0';
  $('bar-clean').style.width = cleanPct.toFixed(1) + '%';
}

// ---- Filters ----
const LEVELS = [
  ['Good', '#123A2B', '#7DEBB5', '#3FCF8E'],
  ['Replace soon', '#3D2E10', '#F2C76E', '#E8B04B'],
  ['Replace now', '#3E1A14', '#FF9A85', '#E0684B'],
];

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function renderFilter(root, name, sub, f) {
  const pct = Math.min(100, (f.usedGal / f.ratingGal) * 100);
  const lv = LEVELS[pct >= 90 ? 2 : pct >= 75 ? 1 : 0];

  const head = el('div', 'f-head');
  const names = el('div');
  names.append(el('span', 'name14', name), el('span', 'label12', sub));
  const pill = el('span', 'pill', lv[0]);
  pill.style.background = lv[1];
  pill.style.color = lv[2];
  head.append(names, pill);

  const line = el('div', 'f-line');
  const used = el('span', 'f-used', fmt(f.usedGal, 0) + ' ');
  used.append(el('small', '', '/ ' + fmt(f.ratingGal, 0) + ' gal'));
  line.append(used, el('span', 'f-pct', fmt(pct, 0) + '%'));

  const track = el('div', 'f-track');
  const fill = el('div', 'f-fill');
  fill.style.width = pct.toFixed(1) + '%';
  fill.style.background = lv[3];
  track.append(fill);

  const foot = el('div', 'f-foot');
  const inst = f.installDate
    ? 'Installed ' + pretty(f.installDate) + ' · ' + daysSince(f.installDate) + ' days'
    : 'Install date not set';
  foot.append(el('span', '', inst), el('span', '', fmt(Math.max(0, f.ratingGal - f.usedGal), 0) + ' gal left'));

  root.replaceChildren(head, line, track, foot);
}

function render() {
  if (!status) return;
  renderTds(status.tds);
  renderVolume();
  const f = status.filters;
  renderFilter($('filter-pre'), 'Sediment + carbon', 'Stages 1–3 · counts total water', f.prefilter);
  renderFilter($('filter-mem'), 'RO membrane',
    'Stage 4 · counts ' + (f.membrane.counts === 'clean' ? 'clean' : 'total') + ' water', f.membrane);
  $('since').textContent = !status.timeValid ? 'Time not synced'
    : status.countingSince ? 'Counting since ' + pretty(status.countingSince) : 'Counting since time sync';
}

// ---- Connection ----
function renderConn() {
  const dot = $('conn-dot');
  const text = $('conn-text');
  if (!lastOkAt) {
    dot.className = 'dot dot-off';
    text.textContent = 'Connecting…';
    return;
  }
  const age = performance.now() - lastOkAt;
  if (age >= STALE_MS) {
    dot.className = 'dot dot-bad';
    text.textContent = 'Disconnected · last update ' + Math.round(age / 1000) + 's ago';
  } else {
    dot.className = 'dot';
    text.textContent = 'Connected · updated ' + Math.round(age / 1000) + 's ago';
  }
}

async function poll() {
  try {
    const r = await fetch('/api/status', { cache: 'no-store', signal: AbortSignal.timeout(5000) });
    if (!r.ok) throw new Error(r.status);
    status = await r.json();
    lastOkAt = performance.now();
    render();
  } catch (e) {
    // renderConn() flips to Disconnected once STALE_MS passes without a good poll
  }
  renderConn();
}

async function post(path, body) {
  const r = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(8000),
  });
  if (!r.ok) {
    let msg = 'Request failed (' + r.status + ')';
    try { msg = (await r.json()).error || msg; } catch (e) { /* keep default */ }
    throw new Error(msg);
  }
}

// ---- Settings dialog ----
const dlg = {
  open() {
    if (!status) return;
    opener = document.activeElement;
    const f = status.filters;
    $('pre-date').value = f.prefilter.installDate || '';
    $('mem-date').value = f.membrane.installDate || '';
    $('pre-rating').value = f.prefilter.ratingGal;
    $('mem-rating').value = f.membrane.ratingGal;
    $('settings-error').hidden = true;
    $('confirm-reset').hidden = true;
    $('reset-done').hidden = true;
    $('ask-reset').hidden = false;
    $('overlay').hidden = false;
    $('pre-date').focus();
  },
  close() {
    $('overlay').hidden = true;
    if (opener) opener.focus();
  },
};

async function saveSettings() {
  const err = $('settings-error');
  const btn = $('save-settings');
  const group = (p) => {
    const g = {};
    if ($(p + '-date').value) g.installDate = $(p + '-date').value;
    const r = Number($(p + '-rating').value);
    if (r > 0) g.ratingGal = r;
    return g;
  };
  btn.disabled = true;
  try {
    await post('/api/settings', { prefilter: group('pre'), membrane: group('mem') });
    dlg.close();
    await poll();
  } catch (e) {
    err.textContent = e.message;
    err.hidden = false;
  } finally {
    btn.disabled = false;
  }
}

async function doReset() {
  $('do-reset').disabled = true;
  try {
    await post('/api/reset-counters');
    $('confirm-reset').hidden = true;
    $('ask-reset').hidden = false;
    $('reset-done').hidden = false;
    await poll();
  } catch (e) {
    $('confirm-reset').hidden = true;
    $('ask-reset').hidden = false;
    $('settings-error').textContent = e.message;
    $('settings-error').hidden = false;
  } finally {
    $('do-reset').disabled = false;
  }
}

function trapTab(e) {
  if (e.key === 'Escape') { dlg.close(); return; }
  if (e.key !== 'Tab') return;
  const items = [...$('dialog').querySelectorAll('button, input')].filter((n) => !n.disabled && n.offsetParent);
  const first = items[0], last = items[items.length - 1];
  if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
  else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
}

// ---- Pull to refresh ----
// Pulling down from the top of the page re-polls /api/status (no full reload).
function initPullToRefresh() {
  const ptr = $('ptr');
  const ico = ptr.firstElementChild;
  const THRESHOLD = 70, MAX = 110;
  let startY = null, dist = 0, busy = false;

  const show = (h, rotate) => {
    ptr.style.height = h + 'px';
    ico.style.opacity = String(Math.min(1, h / 40));
    ico.style.transform = 'rotate(' + rotate + 'deg)';
  };
  const reset = () => {
    ptr.style.transition = 'height 0.2s';
    ptr.classList.remove('ready', 'busy');
    show(0, 0);
    setTimeout(() => { ptr.style.transition = ''; }, 200);
  };

  window.addEventListener('touchstart', (e) => {
    startY = !busy && window.scrollY <= 0 && $('overlay').hidden && e.touches.length === 1 ? e.touches[0].clientY : null;
    dist = 0;
  }, { passive: true });

  window.addEventListener('touchmove', (e) => {
    if (startY === null) return;
    const dy = e.touches[0].clientY - startY;
    if (dy <= 0 || window.scrollY > 0) { dist = 0; show(0, 0); return; }
    dist = Math.min(MAX, dy * 0.5);
    ptr.classList.toggle('ready', dist >= THRESHOLD * 0.5 + 8);
    show(dist, dist * 4);
  }, { passive: true });

  const end = async () => {
    if (startY === null) return;
    startY = null;
    if (dist < THRESHOLD * 0.5 + 8) { reset(); return; }
    busy = true;
    ptr.classList.add('busy');
    show(48, 0);
    await Promise.all([poll(), new Promise((r) => setTimeout(r, 500))]);
    busy = false;
    reset();
  };
  window.addEventListener('touchend', end);
  window.addEventListener('touchcancel', end);
}

function init() {
  document.querySelectorAll('[data-period]').forEach((b) => {
    b.addEventListener('click', () => {
      period = b.dataset.period;
      document.querySelectorAll('[data-period]').forEach((o) => o.setAttribute('aria-pressed', String(o === b)));
      renderVolume();
    });
  });
  $('open-settings').addEventListener('click', dlg.open);
  $('close-settings').addEventListener('click', dlg.close);
  $('cancel-settings').addEventListener('click', dlg.close);
  $('save-settings').addEventListener('click', saveSettings);
  $('ask-reset').addEventListener('click', () => {
    $('ask-reset').hidden = true;
    $('reset-done').hidden = true;
    $('confirm-reset').hidden = false;
    $('cancel-reset').focus();
  });
  $('cancel-reset').addEventListener('click', () => {
    $('confirm-reset').hidden = true;
    $('ask-reset').hidden = false;
    $('ask-reset').focus();
  });
  $('do-reset').addEventListener('click', doReset);
  $('overlay').addEventListener('keydown', trapTab);
  $('overlay').addEventListener('mousedown', (e) => { if (e.target === $('overlay')) dlg.close(); });

  initPullToRefresh();
  poll();
  setInterval(poll, POLL_MS);
  setInterval(renderConn, 1000);
}

init();
