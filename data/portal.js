async function api(path, opt) {
  const r = await fetch(path, opt);
  if (!r.ok) {
    const t = await r.text();
    throw new Error(t || ("HTTP " + r.status));
  }
  const ct = r.headers.get('content-type') || '';
  if (ct.includes('application/json')) return await r.json();
  return await r.text();
}

function qs(sel) { return document.querySelector(sel) }

function setStaticVisibility() {
  const dh = qs('[name="dhcp"]');
  const box = qs('#staticFields');
  if (!dh || !box) return;
  box.style.display = (dh.value === 'false') ? 'block' : 'none';
}

async function ensureAuth() {
  try { await api('/api/me'); return true; } catch (e) { location.href = '/login'; return false; }
}

async function loginSubmit(ev) {
  ev.preventDefault();
  const user = qs('#user').value;
  const pass = qs('#pass').value;
  try {
    await api('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: `user=${encodeURIComponent(user)}&pass=${encodeURIComponent(pass)}` });
    location.href = '/home';
  } catch (e) {
    qs('#msg').textContent = 'Login gagal';
    qs('#msg').className = 'err';
  }
}

async function homeInit() {
  if (!await ensureAuth()) return;
  const st = await api('/api/status');
  qs('#deviceId').textContent = st.iddev || '-';
  qs('#ip').textContent = st.ip || '-';
  qs('#online').textContent = st.online ? 'Online' : 'Offline';
}

async function settingInit() {
  if (!await ensureAuth()) return;
  const c = await api('/api/config');

  const map = {
    ssid: 'ssid',
    password: 'password',
    dhcp: 'dhcp',
    ip: 'ip',
    gateway: 'gateway',
    subnet: 'subnet',
    host: 'host',
    port: 'port',
    topic_subscribe: 'topic_subscribe',
    topic_publish: 'topic_publish',
    iddev: 'iddev',
    offsetday: 'offsetday',
    modeDeviceData: 'modeDeviceData'
  };

  for (const k of Object.keys(map)) {
    const el = qs(`[name="${map[k]}"]`);
    if (!el) continue;
    el.value = (c[k] ?? '');
  }

  const dh = qs('[name="dhcp"]');
  if (dh) {
    dh.addEventListener('change', setStaticVisibility);
  }
  setStaticVisibility();
}

async function settingSubmit(ev) {
  ev.preventDefault();
  const fd = new FormData(ev.target);
  const p = new URLSearchParams();
  for (const [k, v] of fd.entries()) p.append(k, v);
  try {
    qs('#msg').textContent = 'Menyimpan...';
    qs('#msg').className = 'small';
    await api('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: p.toString() });
    qs('#msg').textContent = 'Tersimpan. Restarting...';
    qs('#msg').className = 'ok';
  } catch (e) {
    qs('#msg').textContent = 'Gagal: ' + e.message;
    qs('#msg').className = 'err';
  }
}

async function logout() {
  try { await api('/api/logout'); } catch (e) { }
  location.href = '/login';
}

window.Portal = { loginSubmit, homeInit, settingInit, settingSubmit, logout };
