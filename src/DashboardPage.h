#pragma once

// Single-page dashboard: plain HTML/CSS/JS, no build step, polls the JSON
// API in WebDashboard.cpp every 2s. Kept in its own header so WebDashboard.cpp
// stays readable.
static const char kDashboardHtml[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Alarm Clock</title>
<style>
  body { font-family: system-ui, sans-serif; max-width: 480px; margin: 0 auto; padding: 1rem; background: #12151a; color: #e8eaed; }
  h1 { font-size: 1.3rem; }
  h2 { font-size: 1rem; color: #9aa5b1; margin-top: 2rem; }
  section { background: #1c2128; border-radius: 10px; padding: 1rem; margin-bottom: 1rem; }
  label { display: block; margin: 0.5rem 0 0.2rem; font-size: 0.85rem; color: #9aa5b1; }
  input[type=text], input[type=password], input[type=number], select, textarea { width: 100%; box-sizing: border-box; padding: 0.5rem; border-radius: 6px; border: 1px solid #3a4450; background: #12151a; color: #e8eaed; font-family: inherit; }
  /* Hour/minute alarm fields: the default width only left room for one
     digit before the browser's native up/down spinner, hiding the second
     digit -- padding around the spinner didn't help since its rendered
     width isn't reserved by padding, it just overlaps it. Removing the
     spinner outright (typing/scrolling still work) is the reliable fix. */
  input[type=number].time-input { width: 3rem; padding: 0.5rem 0.3rem; text-align: center; -moz-appearance: textfield; appearance: textfield; }
  input[type=number].time-input::-webkit-outer-spin-button,
  input[type=number].time-input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
  button { padding: 0.5rem 0.9rem; border-radius: 6px; border: none; background: #3a6df0; color: white; margin: 0.2rem 0.3rem 0.2rem 0; cursor: pointer; }
  button.secondary { background: #3a4450; }
  a.button { display: inline-block; text-decoration: none; padding: 0.5rem 0.9rem; border-radius: 6px; background: #3a4450; color: #e8eaed; margin: 0.2rem 0.3rem 0.2rem 0; }
  .row { display: flex; align-items: center; gap: 0.5rem; flex-wrap: wrap; }
  .days { display: flex; gap: 0.3rem; }
  .days label { display: flex; flex-direction: column; align-items: center; font-size: 0.7rem; margin: 0; }
  .status { font-size: 0.85rem; color: #9aa5b1; }
  .status.low-battery { color: #e05252; }
  .alarm-ringing { border: 2px solid #e05252; }
  textarea { height: 5rem; font-size: 0.75rem; }
  .credential-banner { display: none; background: #2a2410; border: 1px solid #d4a017; border-radius: 10px; padding: 0.85rem; margin-bottom: 1rem; font-size: 0.85rem; }
  .credential-banner code { background: #12151a; padding: 0.15rem 0.4rem; border-radius: 4px; }
</style>
</head>
<body>
<h1>ESP32 Radio Alarm Clock</h1>
<div class="status" id="statusLine">Loading&hellip;</div>
<div class="credential-banner" id="credentialBanner"></div>

<section id="wifiSection">
  <h2>WiFi</h2>
  <div class="row">
    <label style="flex:1">SSID<input type="text" id="wifiSsid"></label>
    <label style="flex:1">Password<input type="password" id="wifiPassword"></label>
  </div>
  <button onclick="saveWifi()">Join network</button>
  <a class="button" href="/update">Firmware update</a>
</section>

<section>
  <h2>Security</h2>
  <div class="row">
    <label style="flex:1">Username<input type="text" id="authUsername"></label>
    <label style="flex:1">New password<input type="password" id="authPassword"></label>
  </div>
  <button onclick="saveSecurity()">Update login</button>
  <div class="status">You'll be asked to log in again with the new credentials.</div>
</section>

<section>
  <h2>Date &amp; Time</h2>
  <label>Time zone
    <select id="tzSelect" onchange="setTimezone()"></select>
  </label>
  <div class="status">Takes effect on the next NTP sync (immediately, if online).</div>
  <label>Clock format</label>
  <button class="secondary" id="timeFormatBtn" onclick="toggleTimeFormat()">--</button>
</section>

<section>
  <h2>Radio</h2>
  <label>Region
    <select id="regionSelect" onchange="setRegion()"></select>
  </label>
  <div class="status">Sets FM de-emphasis and tuning band for your region.</div>
  <div class="row">
    <span id="radioFreq" style="font-size:1.4rem">--.- MHz</span>
    <button onclick="radioAction('seekDown')">&laquo; Seek</button>
    <button onclick="radioAction('seekUp')">Seek &raquo;</button>
  </div>
  <div class="status" id="rdsStatus"></div>
  <label>Tune (MHz)
    <input type="number" id="tuneInput" step="0.1" min="87.5" max="108.0">
  </label>
  <button onclick="tuneRadio()">Tune</button>
  <label>Volume: <span id="volumeValue">--</span></label>
  <input type="range" id="volumeSlider" min="0" max="63" oninput="setVolume(this.value)">
  <button class="secondary" id="muteBtn" onclick="toggleMute()">Mute</button>
  <div id="presets" class="row"></div>
  <label>Sleep timer</label>
  <div class="row">
    <select id="sleepMinutes">
      <option value="15">15 min</option>
      <option value="30">30 min</option>
      <option value="45">45 min</option>
      <option value="60">60 min</option>
    </select>
    <button class="secondary" onclick="radioAction('sleepTimer', parseInt(document.getElementById('sleepMinutes').value, 10))">Set</button>
    <button class="secondary" onclick="radioAction('sleepTimer', 0)">Cancel</button>
    <span id="sleepStatus" class="status"></span>
  </div>
</section>

<section>
  <h2>Alarms</h2>
  <div id="alarmList"></div>
  <div class="row">
    <button onclick="alarmAction('snooze')">Snooze</button>
    <input type="number" id="snoozeMinutes" min="1" max="60" class="time-input" title="Snooze duration (minutes)">
    <span class="status">min</span>
    <button class="secondary" onclick="saveSnoozeMinutes()">Save</button>
    <button class="secondary" onclick="alarmAction('dismiss')">Dismiss</button>
  </div>
</section>

<section>
  <h2>Settings backup</h2>
  <textarea id="settingsBlob" placeholder="Export shows alarms + radio presets/volume as JSON here. Paste a previous export and Apply to restore it."></textarea>
  <div class="row">
    <button class="secondary" onclick="exportSettings()">Export</button>
    <button onclick="importSettings()">Apply</button>
  </div>
</section>

<script>
const dayLabels = ['Su','Mo','Tu','We','Th','Fr','Sa'];

async function api(path, options) {
  const res = await fetch(path, options);
  return res.json();
}

const wakeSources = [['radio', 'Radio'], ['beep', 'Beep'], ['chime', 'Chime']];

// 24h stored/transmitted value <-> 12h digit + AM/PM, for alarm display
// when status.is24HourFormat is false (mirrors MenuSystem's TFT format).
function to12Hour(h24) {
  const period = h24 < 12 ? 'AM' : 'PM';
  let h12 = h24 % 12;
  if (h12 === 0) h12 = 12;
  return { h12, period };
}
function to24Hour(h12, period) {
  const h = h12 % 12; // 12 AM/PM -> 0
  return period === 'PM' ? h + 12 : h;
}

let rebooting = false;
let is24HourFormat = true;  // kept in sync by refresh(); toggleTimeFormat() flips this

// Was 5 separate requests (status/security/timezone/radio/alarms) every
// 2s -- each one blocks on the device's shared state lock until loop()
// releases it, and that much sustained contention was enough to trip the
// async_tcp task's watchdog and reboot the device. One combined endpoint
// cuts that 5x. The timezone dropdown's option list is loaded once
// separately (see loadTimezoneOptions()), since it never changes at
// runtime and doesn't need to ride along on every poll.
async function refresh() {
  if (rebooting) return;
  try {
    const status = await api('/api/status');
    const battery = status.batteryPercent === undefined ? '' :
      ` · battery: ${status.batteryPercent.toFixed(0)}%${status.batteryLow ? ' (low!)' : ''}`;
    const statusEl = document.getElementById('statusLine');
    statusEl.textContent =
      `${status.mode === 'ap' ? 'Setup mode' : 'Connected: ' + status.ssid} · ${status.ip} · ${status.time || 'no RTC'} · alarm: ${status.alarmState}${battery}`;
    statusEl.className = 'status' + (status.batteryLow ? ' low-battery' : '');

    const banner = document.getElementById('credentialBanner');
    if (status.dashboardPassword) {
      banner.style.display = 'block';
      banner.innerHTML = `<strong>Save this dashboard login</strong> &mdash; you'll need it once connected to WiFi: ` +
        `username <code>${status.dashboardUsername}</code>, password <code>${status.dashboardPassword}</code>. Change it below under Security.`;
    } else {
      banner.style.display = 'none';
    }

    const usernameField = document.getElementById('authUsername');
    if (document.activeElement !== usernameField) usernameField.value = status.security.username;

    const tzSelect = document.getElementById('tzSelect');
    if (tzSelect.dataset.loaded) tzSelect.value = status.timezoneIndex;
    is24HourFormat = status.is24HourFormat;
    document.getElementById('timeFormatBtn').textContent =
      is24HourFormat ? 'Switch to 12-hour' : 'Switch to 24-hour';

    const radio = status.radio;
    const regionSelect = document.getElementById('regionSelect');
    if (regionSelect.dataset.loaded) regionSelect.value = radio.regionIndex;
    document.getElementById('radioFreq').textContent = (radio.frequency10kHz / 100).toFixed(1) + ' MHz';
    document.getElementById('rdsStatus').textContent =
      [radio.stationName, radio.radioText].filter(Boolean).join(' — ');
    document.getElementById('volumeValue').textContent = radio.volume;
    // Same clobbering guard as the alarm list/username field above --
    // don't yank the slider mid-drag.
    const volumeSlider = document.getElementById('volumeSlider');
    if (document.activeElement !== volumeSlider) volumeSlider.value = radio.volume;
    document.getElementById('muteBtn').textContent = radio.muted ? 'Unmute' : 'Mute';
    document.getElementById('sleepStatus').textContent =
      radio.sleepTimerMinutes > 0 ? `${radio.sleepTimerMinutes} min left` : '';
    const presetsEl = document.getElementById('presets');
    presetsEl.innerHTML = '';
    radio.presets.forEach((freq, i) => {
      const wrap = document.createElement('span');
      wrap.style.display = 'inline-flex';
      wrap.style.marginRight = '0.3rem';

      const b = document.createElement('button');
      b.className = 'secondary';
      b.style.margin = '0.2rem 0';
      b.textContent = freq ? (freq / 100).toFixed(1) : `Set ${i + 1}`;
      b.onclick = () => freq ? radioPreset('recall', i) : radioPreset('store', i);
      wrap.appendChild(b);

      if (freq) {
        // Tapping the preset itself always recalls it once assigned -- this
        // is the only way to overwrite it with the current frequency.
        const reassign = document.createElement('button');
        reassign.className = 'secondary';
        reassign.title = 'Reassign to current frequency';
        reassign.textContent = '✎';
        reassign.style.margin = '0.2rem 0 0.2rem 1px';
        reassign.style.padding = '0.5rem 0.6rem';
        reassign.onclick = () => radioPreset('store', i);
        wrap.appendChild(reassign);
      }

      presetsEl.appendChild(wrap);
    });

    const alarms = status.alarms;
    const snoozeMinutesInput = document.getElementById('snoozeMinutes');
    if (document.activeElement !== snoozeMinutesInput) snoozeMinutesInput.value = alarms.snoozeMinutes;
    const list = document.getElementById('alarmList');
    // Skip the rebuild while a field inside the list is being edited, so an
    // in-progress edit isn't wiped out by a poll that lands mid-keystroke.
    // Doesn't apply to the Save button itself (not INPUT/SELECT), so the
    // list still refreshes normally right after saving.
    const active = document.activeElement;
    const editing = list.contains(active) && (active.tagName === 'INPUT' || active.tagName === 'SELECT');
    if (!editing) {
      list.innerHTML = '';
      const use12h = !status.is24HourFormat;
      alarms.alarms.forEach((a, i) => {
        const div = document.createElement('div');
        div.className = 'row' + (status.alarmState !== 'idle' && alarms.ringingIndex === i ? ' alarm-ringing' : '');
        const hour12 = use12h ? to12Hour(a.hour) : null;
        const hourValue = use12h ? hour12.h12 : a.hour;
        const hourMin = use12h ? 1 : 0;
        const hourMax = use12h ? 12 : 23;
        const ampmSelect = use12h ? `
          <select id="p${i}" style="width:auto">
            <option value="AM" ${hour12.period === 'AM' ? 'selected' : ''}>AM</option>
            <option value="PM" ${hour12.period === 'PM' ? 'selected' : ''}>PM</option>
          </select>` : '';
        div.innerHTML = `
          <input type="checkbox" ${a.enabled ? 'checked' : ''} onchange="updateAlarm(${i})" id="en${i}">
          <input type="number" value="${hourValue}" min="${hourMin}" max="${hourMax}" class="time-input" id="h${i}">:
          <input type="number" value="${a.minute}" min="0" max="59" class="time-input" id="m${i}">
          ${ampmSelect}
          <span class="days">${dayLabels.map((d, di) => `<label><input type="checkbox" ${a.days[di] ? 'checked' : ''} id="d${i}_${di}">${d}</label>`).join('')}</span>
          <select id="w${i}" style="width:auto">${wakeSources.map(([v, l]) => `<option value="${v}" ${a.wakeSource === v ? 'selected' : ''}>${l}</option>`).join('')}</select>
          <button onclick="updateAlarm(${i})">Save</button>`;
        list.appendChild(div);
      });
    }
  } catch (e) { /* device may be mid-reboot after WiFi save */ }
}

// The option list itself is static (never changes at runtime), so it's
// fetched once here rather than riding along on every 2s poll -- refresh()
// still keeps the selected value in sync via status.timezoneIndex.
async function loadTimezoneOptions() {
  try {
    const tz = await api('/api/timezone');
    const tzSelect = document.getElementById('tzSelect');
    tzSelect.innerHTML = tz.options.map((label, i) => `<option value="${i}">${label}</option>`).join('');
    tzSelect.value = tz.index;
    tzSelect.dataset.loaded = 'true';
  } catch (e) {}
}

// Same one-time-load pattern as loadTimezoneOptions() above.
async function loadRegionOptions() {
  try {
    const radio = await api('/api/radio');
    const regionSelect = document.getElementById('regionSelect');
    regionSelect.innerHTML = radio.regionOptions.map((label, i) => `<option value="${i}">${label}</option>`).join('');
    regionSelect.value = radio.regionIndex;
    regionSelect.dataset.loaded = 'true';
  } catch (e) {}
}

async function saveWifi() {
  await api('/api/wifi', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: document.getElementById('wifiSsid').value, password: document.getElementById('wifiPassword').value }) });
  // The device waits ~1.5s before actually rebooting, so without this the
  // next periodic poll could land in that window and flicker this message
  // back to a stale status right before the connection drops anyway.
  rebooting = true;
  document.getElementById('statusLine').textContent = 'Saved. Rebooting to join network...';
}

async function saveSecurity() {
  const password = document.getElementById('authPassword').value;
  if (!password) { alert('Enter a new password.'); return; }
  await api('/api/security', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username: document.getElementById('authUsername').value, password }) });
  document.getElementById('authPassword').value = '';
  alert('Login updated. Reload the page and sign in with the new credentials.');
}

async function setTimezone() {
  const index = parseInt(document.getElementById('tzSelect').value, 10);
  await api('/api/timezone', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ index }) });
  refresh();
}

async function toggleTimeFormat() {
  await api('/api/timeformat', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ is24Hour: !is24HourFormat }) });
  refresh();
}

async function setRegion() {
  const index = parseInt(document.getElementById('regionSelect').value, 10);
  await radioAction('setRegion', index);
}

function tuneRadio() {
  const mhz = parseFloat(document.getElementById('tuneInput').value);
  if (!isNaN(mhz)) radioAction('tune', Math.round(mhz * 100));
}
function setVolume(v) { radioAction('volume', parseInt(v, 10)); }
function toggleMute() { radioAction('toggleMute'); }
function radioPreset(action, index) { radioAction(action === 'recall' ? 'recallPreset' : 'storePreset', index); }

async function radioAction(action, value) {
  await api('/api/radio', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action, value }) });
  refresh();
}

async function updateAlarm(i) {
  const days = dayLabels.map((_, di) => document.getElementById(`d${i}_${di}`).checked);
  let hour = parseInt(document.getElementById(`h${i}`).value, 10);
  const periodEl = document.getElementById(`p${i}`);  // only present in 12h format
  if (periodEl) hour = to24Hour(hour, periodEl.value);
  await api('/api/alarms', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      index: i,
      enabled: document.getElementById(`en${i}`).checked,
      hour,
      minute: parseInt(document.getElementById(`m${i}`).value, 10),
      wakeSource: document.getElementById(`w${i}`).value,
      days,
    }) });
  refresh();
}

async function alarmAction(action) {
  await api('/api/alarm/' + action, { method: 'POST' });
  refresh();
}

async function saveSnoozeMinutes() {
  const minutes = parseInt(document.getElementById('snoozeMinutes').value, 10);
  await api('/api/alarms/snooze-minutes', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ minutes }) });
  refresh();
}

async function exportSettings() {
  const settings = await api('/api/settings');
  document.getElementById('settingsBlob').value = JSON.stringify(settings, null, 2);
}

async function importSettings() {
  try {
    const parsed = JSON.parse(document.getElementById('settingsBlob').value);
    await api('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(parsed) });
    refresh();
  } catch (e) {
    alert('Invalid JSON: ' + e.message);
  }
}

loadTimezoneOptions();
loadRegionOptions();
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)rawliteral";
