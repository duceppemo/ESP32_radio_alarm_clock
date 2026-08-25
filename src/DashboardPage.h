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
  input[type=text], input[type=password], input[type=number] { width: 100%; box-sizing: border-box; padding: 0.5rem; border-radius: 6px; border: 1px solid #3a4450; background: #12151a; color: #e8eaed; }
  button { padding: 0.5rem 0.9rem; border-radius: 6px; border: none; background: #3a6df0; color: white; margin: 0.2rem 0.3rem 0.2rem 0; cursor: pointer; }
  button.secondary { background: #3a4450; }
  .row { display: flex; align-items: center; gap: 0.5rem; flex-wrap: wrap; }
  .days { display: flex; gap: 0.3rem; }
  .days label { display: flex; flex-direction: column; align-items: center; font-size: 0.7rem; margin: 0; }
  .status { font-size: 0.85rem; color: #9aa5b1; }
  .alarm-ringing { border: 2px solid #e05252; }
</style>
</head>
<body>
<h1>ESP32 Radio Alarm Clock</h1>
<div class="status" id="statusLine">Loading&hellip;</div>

<section id="wifiSection">
  <h2>WiFi</h2>
  <div class="row">
    <label style="flex:1">SSID<input type="text" id="wifiSsid"></label>
    <label style="flex:1">Password<input type="password" id="wifiPassword"></label>
  </div>
  <button onclick="saveWifi()">Join network</button>
</section>

<section>
  <h2>Radio</h2>
  <div class="row">
    <span id="radioFreq" style="font-size:1.4rem">--.- MHz</span>
    <button onclick="radioAction('seekDown')">&laquo; Seek</button>
    <button onclick="radioAction('seekUp')">Seek &raquo;</button>
  </div>
  <label>Tune (MHz)
    <input type="number" id="tuneInput" step="0.1" min="87.5" max="108.0">
  </label>
  <button onclick="tuneRadio()">Tune</button>
  <label>Volume: <span id="volumeValue">--</span></label>
  <input type="range" id="volumeSlider" min="0" max="63" oninput="setVolume(this.value)">
  <button class="secondary" id="muteBtn" onclick="toggleMute()">Mute</button>
  <div id="presets" class="row"></div>
</section>

<section>
  <h2>Alarms</h2>
  <div id="alarmList"></div>
  <div class="row">
    <button onclick="alarmAction('snooze')">Snooze</button>
    <button class="secondary" onclick="alarmAction('dismiss')">Dismiss</button>
  </div>
</section>

<script>
const dayLabels = ['Su','Mo','Tu','We','Th','Fr','Sa'];

async function api(path, options) {
  const res = await fetch(path, options);
  return res.json();
}

async function refresh() {
  try {
    const status = await api('/api/status');
    document.getElementById('statusLine').textContent =
      `${status.mode === 'ap' ? 'Setup mode' : 'Connected: ' + status.ssid} · ${status.ip} · ${status.time || 'no RTC'} · alarm: ${status.alarmState}`;
  } catch (e) { /* device may be mid-reboot after WiFi save */ }

  try {
    const radio = await api('/api/radio');
    document.getElementById('radioFreq').textContent = (radio.frequency10kHz / 100).toFixed(1) + ' MHz';
    document.getElementById('volumeValue').textContent = radio.volume;
    document.getElementById('volumeSlider').value = radio.volume;
    document.getElementById('muteBtn').textContent = radio.muted ? 'Unmute' : 'Mute';
    const presetsEl = document.getElementById('presets');
    presetsEl.innerHTML = '';
    radio.presets.forEach((freq, i) => {
      const b = document.createElement('button');
      b.className = 'secondary';
      b.textContent = freq ? (freq / 100).toFixed(1) : `Set ${i + 1}`;
      b.onclick = () => freq ? radioPreset('recall', i) : radioPreset('store', i);
      presetsEl.appendChild(b);
    });
  } catch (e) {}

  try {
    const alarms = await api('/api/alarms');
    const list = document.getElementById('alarmList');
    list.innerHTML = '';
    alarms.alarms.forEach((a, i) => {
      const div = document.createElement('div');
      div.className = 'row' + (alarms.state !== 'idle' && alarms.ringingIndex === i ? ' alarm-ringing' : '');
      div.innerHTML = `
        <input type="checkbox" ${a.enabled ? 'checked' : ''} onchange="updateAlarm(${i})" id="en${i}">
        <input type="number" value="${a.hour}" min="0" max="23" style="width:3.5em" id="h${i}">:
        <input type="number" value="${a.minute}" min="0" max="59" style="width:3.5em" id="m${i}">
        <span class="days">${dayLabels.map((d, di) => `<label><input type="checkbox" ${a.days[di] ? 'checked' : ''} id="d${i}_${di}">${d}</label>`).join('')}</span>
        <button onclick="updateAlarm(${i})">Save</button>`;
      list.appendChild(div);
    });
  } catch (e) {}
}

async function saveWifi() {
  await api('/api/wifi', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: document.getElementById('wifiSsid').value, password: document.getElementById('wifiPassword').value }) });
  document.getElementById('statusLine').textContent = 'Saved. Rebooting to join network...';
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
  await api('/api/alarms', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      index: i,
      enabled: document.getElementById(`en${i}`).checked,
      hour: parseInt(document.getElementById(`h${i}`).value, 10),
      minute: parseInt(document.getElementById(`m${i}`).value, 10),
      days,
    }) });
  refresh();
}

async function alarmAction(action) {
  await api('/api/alarm/' + action, { method: 'POST' });
  refresh();
}

refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)rawliteral";
