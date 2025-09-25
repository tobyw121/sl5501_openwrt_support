const state = {
  sid: null,
  token: null,
  statusTimer: null,
};

async function ubusCall(object, method, params = {}) {
  if (!state.token && !(object === 'session' && method === 'login')) {
    throw new Error('Not authenticated');
  }

  const payload = {
    jsonrpc: '2.0',
    id: Date.now(),
    method: 'call',
    params: [state.token, object, method, params],
  };

  const response = await fetch('/ubus', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  const data = await response.json();
  if (!data.result || data.result[0] !== 0) {
    const code = data.error ? data.error.code : data.result?.[0];
    throw new Error(`ubus error ${code}`);
  }

  return data.result[1];
}

function formatUptime(seconds) {
  const s = Math.floor(seconds);
  const days = Math.floor(s / 86400);
  const hours = Math.floor((s % 86400) / 3600);
  const minutes = Math.floor((s % 3600) / 60);
  const parts = [];
  if (days) parts.push(`${days}d`);
  if (hours) parts.push(`${hours}h`);
  parts.push(`${minutes}m`);
  return parts.join(' ');
}

function formatLoad(info) {
  return [info.load1, info.load5, info.load15]
    .map((n) => Number(n).toFixed(2))
    .join(' / ');
}

function formatMemory(info) {
  const mem = info.memory || {};
  const total = mem.total ? mem.total / 1024 / 1024 : 0;
  const free = mem.available ? mem.available / 1024 / 1024 : 0;
  return `${free.toFixed(1)} / ${total.toFixed(1)} MB free`;
}

function show(element, visible) {
  element.hidden = !visible;
}

function setMessage(el, text, type = 'info') {
  el.textContent = text;
  el.classList.toggle('error', type === 'error');
  el.classList.toggle('message', type !== 'error');
  el.hidden = !text;
}

async function handleLogin(event) {
  event.preventDefault();
  const username = document.getElementById('username').value;
  const password = document.getElementById('password').value;
  const errorEl = document.getElementById('login-error');
  setMessage(errorEl, '');

  try {
    const payload = {
      jsonrpc: '2.0',
      id: Date.now(),
      method: 'call',
      params: [null, 'session', 'login', { username, password }],
    };

    const res = await fetch('/ubus', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();
    if (!data.result || data.result[0] !== 0) {
      throw new Error('Invalid credentials');
    }

    state.sid = data.result[1].ubus_rpc_session;
    state.token = state.sid;

    show(document.getElementById('login-section'), false);
    ['dashboard', 'network', 'upgrade'].forEach((id) => show(document.getElementById(id), true));
    await Promise.all([refreshStatus(), loadLanConfig()]);
    scheduleStatusRefresh();
  } catch (err) {
    console.error(err);
    setMessage(errorEl, err.message || 'Login failed', 'error');
  }
}

async function refreshStatus() {
  try {
    const info = await ubusCall('miniui', 'status', {});
    document.getElementById('hostname').textContent = info.hostname || 'unknown';
    document.getElementById('uptime').textContent = info.uptime ? formatUptime(info.uptime) : '-';
    document.getElementById('load').textContent = formatLoad(info);
    document.getElementById('kernel').textContent = `${info.kernel || ''} (${info.machine || ''})`;
    document.getElementById('memory').textContent = formatMemory(info);
    setMessage(document.getElementById('status-error'), '');
  } catch (err) {
    console.error(err);
    setMessage(document.getElementById('status-error'), err.message || 'Failed to read status', 'error');
  }
}

function scheduleStatusRefresh() {
  if (state.statusTimer) {
    clearInterval(state.statusTimer);
  }
  state.statusTimer = setInterval(refreshStatus, 10000);
}

async function loadLanConfig() {
  try {
    const result = await ubusCall('uci', 'get', { config: 'network', section: 'lan' });
    const values = result?.values || {};
    document.getElementById('lan-ip').value = values.ipaddr || '';
    document.getElementById('lan-mask').value = values.netmask || '';
    setMessage(document.getElementById('lan-message'), '');
  } catch (err) {
    console.error(err);
    setMessage(document.getElementById('lan-message'), err.message || 'Unable to load LAN settings', 'error');
  }
}

async function submitLan(event) {
  event.preventDefault();
  const ip = document.getElementById('lan-ip').value.trim();
  const mask = document.getElementById('lan-mask').value.trim();
  const message = document.getElementById('lan-message');
  setMessage(message, '');

  try {
    await ubusCall('miniui', 'apply_lan', { ipaddr: ip, netmask: mask });
    await ubusCall('uci', 'commit', { config: 'network' });
    setMessage(message, 'LAN settings saved');
  } catch (err) {
    console.error(err);
    setMessage(message, err.message || 'Failed to save LAN settings', 'error');
  }
}

async function reloadNetwork() {
  const message = document.getElementById('lan-message');
  setMessage(message, '');
  try {
    await ubusCall('miniui', 'reload_network', {});
    setMessage(message, 'Network reload triggered');
  } catch (err) {
    console.error(err);
    setMessage(message, err.message || 'Failed to reload network', 'error');
  }
}

async function rebootSystem() {
  try {
    await ubusCall('system', 'reboot', {});
    setMessage(document.getElementById('status-error'), 'Reboot requested');
  } catch (err) {
    console.error(err);
    setMessage(document.getElementById('status-error'), err.message || 'Failed to reboot', 'error');
  }
}

async function triggerUpgrade(event) {
  event.preventDefault();
  const source = document.getElementById('upgrade-source').value.trim();
  const keep = document.getElementById('upgrade-keep').checked;
  const message = document.getElementById('upgrade-message');
  setMessage(message, '');

  if (!source) {
    setMessage(message, 'Firmware path or URL required', 'error');
    return;
  }

  try {
    await ubusCall('miniui', 'sysupgrade', { source, keep });
    setMessage(message, 'Upgrade started. Device will reboot when finished.');
  } catch (err) {
    console.error(err);
    setMessage(message, err.message || 'Upgrade failed to start', 'error');
  }
}

function bindEvents() {
  document.getElementById('login-form').addEventListener('submit', handleLogin);
  document.getElementById('lan-form').addEventListener('submit', submitLan);
  document.getElementById('lan-reload').addEventListener('click', reloadNetwork);
  document.getElementById('reboot-btn').addEventListener('click', rebootSystem);
  document.getElementById('upgrade-form').addEventListener('submit', triggerUpgrade);
}

bindEvents();
