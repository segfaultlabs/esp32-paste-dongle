// ESP32 paste dongle: BLE or USB HID keyboard controlled via WiFi AP + web UI.
// Compile with HID_BACKEND_BLE for classic ESP32, or HID_BACKEND_USB for ESP32-S3.

#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>  // for esp_ota_mark_app_valid_cancel_rollback
#include <freertos/queue.h>
#include "hid/ihid_backend.h"
#ifdef HID_BACKEND_USB
#include "hid/usb_hid.h"  // also declares g_rtc_usb_mode
#endif
#include "keymap.h"
#include "config_store.h"
#include "typing_engine.h"
#include "paster.h"
#include "mouse_jiggler.h"
#include "macro_parser.h"
#include "macro_runner.h"
#include "snippet_store.h"
#include "led_controller.h"
#include "human_sim.h"

static hid::IHidBackend* backend = nullptr;
static bool was_connected = false;
static config::Store cfg;
static snippet::Store snippets;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dns_server;
static paster::Paster* g_paster = nullptr;
static jiggler::Jiggler* g_jiggler = nullptr;
static macro::Runner* g_macro_runner = nullptr;
static sim::HumanSim* g_sim = nullptr;
static led::Controller g_led;

// Command queue: WS-handler task pushes, main loop drains.
// This eliminates data races between the AsyncTCP task and loop().
struct WsCmd {
    enum class Type { START, CHUNK, CANCEL, JIGGLE_ENABLE, JIGGLE_CFG, JIGGLE_GET,
                      MACRO_RUN, MACRO_CANCEL } type;
    uint32_t client_id = 0;
    std::string text;
    int total = -1;
    typing::Config typing_cfg;
    bool flag = false;
    jiggler::Settings jiggle_cfg;
    std::vector<macro::Command> macro_cmds;
};
static QueueHandle_t g_cmd_queue = nullptr;
static const int CMD_QUEUE_DEPTH = 24;

// Backpressure: ACK a chunk to the client only once the paste buffer drains
// below this threshold so the phone can't flood RAM faster than we type.
static const int PASTE_HIGH_WATER = 3072;
static const int PASTE_LOW_WATER  = 1024;
static uint32_t g_ack_client_id = 0;
static bool g_pending_ack = false;

static unsigned long last_status_ms = 0;
static int last_reported_typed = -1;
static paster::State last_reported_state = paster::State::IDLE;

// Minimal fallback served when LittleFS has no index.html yet.
// The full UI lives in data/index.html and is uploaded via `pio run -t uploadfs`
// or via HTTP POST /api/fs/upload after the first serial flash.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Paste Dongle — setup needed</title>
<style>
body{font-family:system-ui,sans-serif;max-width:540px;margin:3em auto;padding:0 1em;}
code{background:#eee;padding:.1em .3em;border-radius:3px;}
.card{background:#f8f9fa;border:1px solid #ddd;border-radius:6px;padding:1em 1.2em;margin:1em 0;}
</style>
</head>
<body>
<h1>PasteDongle</h1>
<p>The UI file hasn't been uploaded to the device filesystem yet.</p>
<div class="card">
  <strong>Option 1 — serial flash (first time):</strong>
  <pre>pio run -t uploadfs -e esp32s3-usb</pre>
</div>
<div class="card">
  <strong>Option 2 — HTTP upload (after first serial flash):</strong>
  <form method="post" enctype="multipart/form-data" action="/api/fs/upload">
    <input type="file" name="file" accept=".html" required>
    <button type="submit">Upload index.html</button>
  </form>
</div>
</body>
</html>
)rawliteral";

// Full INDEX_HTML placeholder — kept to avoid removing the rawliteral macro.
// The real HTML lives in data/index.html served from LittleFS.
static const char INDEX_HTML_UNUSED[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Paste Dongle</title>
<style>
body { font-family: system-ui, sans-serif; max-width: 640px; margin: 2em auto; padding: 0 1em; }
h2 { margin-top: 1.8em; border-bottom: 1px solid #ddd; padding-bottom: 0.2em; }
h3 { margin-top: 1.2em; }
label { display: block; margin: 0.9em 0 0.2em; }
input[type="text"], input[type="number"], select, textarea { width: 100%; padding: 0.5em; box-sizing: border-box; font: inherit; }
input[type="range"] { width: 100%; }
textarea { resize: vertical; }
button { padding: 0.6em 1.2em; margin: 0.5em 0.5em 0 0; }
#progressWrap { width: 100%; background: #e9ecef; border-radius: 0.25em; overflow: hidden; }
#progressBar { width: 0%; height: 1.2em; background: #007bff; transition: width 0.2s; }
.status { color: #555; margin-top: 0.5em; }
#stats { font-size: 0.9em; color: #666; }
.hint { font-size: 0.82em; color: #777; margin: 0.1em 0 0.5em; line-height: 1.4; }
.card { background: #f8f9fa; border: 1px solid #e4e4e4; border-radius: 6px; padding: 0.9em 1.1em; margin: 0.8em 0; }
code { background: #eee; border-radius: 3px; padding: 0.1em 0.3em; font-size: 0.88em; }
</style>
</head>
<body>
<h1>ESP32 Paste Dongle</h1>
<p id="deviceInfoBar" style="color:#888;font-size:0.85em;margin:-0.8em 0 1em;">Connecting...</p>

<section id="bleSection">
<h2>Bluetooth name</h2>
<p class="hint">The name your device advertises over Bluetooth LE. Your host computer sees it as a keyboard with this name — useful for disguising the dongle as a known device. Requires a reboot to take effect.</p>
<p>Current name: <strong id="cur">...</strong></p>
<form id="cfgform">
  <label for="name">Advertise as</label>
  <input type="text" id="name" name="name" maxlength="31" placeholder="e.g. Logitech K380">
  <button type="submit">Save &amp; Reboot</button>
</form>
</section>

<h2>Keyboard layout</h2>
<p class="hint">Must match the keyboard layout configured in your host OS (System Settings → Keyboard). If this is wrong, characters like <code>@</code> <code>"</code> <code>#</code> and <code>\</code> will be typed incorrectly. When in doubt, use US.</p>
<p>Active layout: <strong id="layoutCur">US</strong></p>
<form id="layoutForm">
  <label for="layout">Host OS layout</label>
  <select id="layout">
    <option value="US">US QWERTY</option>
    <option value="UK">UK QWERTY</option>
    <option value="DVORAK">Dvorak</option>
  </select>
  <p class="hint">Changes take effect immediately — no reboot needed.</p>
  <button type="submit">Save layout</button>
</form>

<h2>Paste into host computer</h2>
<p class="hint">Paste text below, then tap <strong>Paste it</strong>. The dongle types it into whatever window is focused on the connected computer. Make sure your target text field is focused before pressing the button.</p>

<label for="mode">Typing speed</label>
<select id="mode">
  <option value="max">Max Speed — as fast as the host can accept (may drop chars in slow apps)</option>
  <option value="fast" selected>Fast Typist — ~120 WPM with slight variation (recommended)</option>
  <option value="human">Human / Careful — ~70 WPM, more variation, safer for web forms</option>
</select>

<label for="text">Text to type</label>
<textarea id="text" rows="10" placeholder="Paste your text here…"></textarea>
<p id="stats">0 chars — estimated time: -</p>
<p id="charWarn" style="display:none; color:#c05000; margin:0.25em 0 0;"></p>

<button id="pasteBtn">Paste it</button>
<button id="cancelBtn" disabled>Cancel</button>

<div id="progressWrap" style="display:none; margin-top:1em;">
  <div id="progressBar"></div>
</div>
<p class="status" id="pasteStatus">Connecting...</p>

<h2>Saved snippets</h2>
<p class="hint">Store up to 8 frequently-used text blocks. Hit <strong>Type</strong> to send one immediately without opening the paste area. Use <strong>Edit</strong> to change a snippet's content. Snippets survive reboots.</p>
<div id="snippetList"><em>Loading...</em></div>
<button id="saveSnippetBtn">Save current text as snippet</button>
<div id="snippetEditArea" style="display:none;margin-top:0.8em;" class="card">
  <strong>Edit snippet</strong>
  <label for="editTitle" style="margin-top:0.6em;">Title</label>
  <input type="text" id="editTitle" maxlength="64">
  <label for="editText" style="margin-top:0.4em;">Text</label>
  <textarea id="editText" rows="6"></textarea>
  <div style="margin-top:0.4em;">
    <button id="saveEditBtn">Save</button>
    <button id="cancelEditBtn">Cancel</button>
  </div>
</div>

<h2>Human simulation</h2>
<p class="hint">Keeps your session alive on applications that track <em>keyboard</em> activity separately from mouse movement (e.g. some VPNs, remote desktop clients, meeting tools). The dongle types random words and erases them in a loop — it looks like someone is at the keyboard.</p>
<p class="status" id="simStatus">Loading...</p>
<label><input type="checkbox" id="simEnabled"> Enable human simulation</label>
<p class="hint">Only runs when the paste and macro engine are both idle. Will not interfere with real pastes.</p>

<label for="simPause">Pause between bursts (seconds)</label>
<input type="number" id="simPause" min="5" max="300" value="18">
<p class="hint">How long the dongle stays quiet between bursts. 18 s is a good default — long enough to be subtle, short enough to prevent idle timeouts on most systems.</p>

<label for="simWords">Words per burst</label>
<input type="number" id="simWords" min="2" max="20" value="8">
<p class="hint">How many words to type before erasing. More words = longer visible activity periods.</p>

<button id="simSaveBtn">Save simulation settings</button>

<h2>Mouse jiggler</h2>
<p class="hint">Moves the mouse cursor periodically to prevent screen lock, sleep, or idle timeouts. The <strong>Natural</strong> mode is specifically designed to be undetectable by anti-idle software.</p>
<p id="jiggleStatus" class="status">Loading...</p>
<label><input type="checkbox" id="jiggleEnabled"> Enable mouse jiggler</label>

<label for="jigglePattern" style="margin-top:0.8em;">Mode</label>
<select id="jigglePattern">
  <option value="natural">Natural — Poisson intervals + organic drift (recommended)</option>
  <option value="random">Random jumps (simple, somewhat detectable)</option>
  <option value="line">Line (back and forth — very detectable)</option>
  <option value="square">Square (geometric — very detectable)</option>
  <option value="circle">Circle (geometric — very detectable)</option>
</select>

<div id="naturalControls">
  <div class="card" style="margin-top:0.6em;">
    <label for="jiggleMeanInterval">Mean interval (seconds): <strong id="jiggleMeanIntervalVal"></strong></label>
    <input type="number" id="jiggleMeanInterval" min="5" max="600" value="30">
    <p class="hint">The average time between movements. Each actual interval is drawn from a Poisson process — sometimes shorter, sometimes much longer — so there is no detectable heartbeat pattern.</p>

    <label for="jiggleDriftRadius" style="margin-top:0.8em;">Drift radius (pixels): <span id="jiggleDriftRadiusVal">5</span></label>
    <input type="range" id="jiggleDriftRadius" min="1" max="20" value="5">
    <p class="hint">Maximum distance the cursor wanders from its starting position. 1–5 px is invisible on a normal monitor at arm's length. Higher values are slightly more "alive" but may be noticeable on very small screens.</p>

    <label for="jiggleJitter" style="margin-top:0.8em;">Jitter level: <span id="jiggleJitterVal">50</span>%</label>
    <input type="range" id="jiggleJitter" min="0" max="100" value="50">
    <p class="hint">Controls how much random noise is applied to each movement step. 0% = cursor barely moves and returns quickly to centre. 100% = cursor actively drifts around within the radius. 40–60% is the sweet spot.</p>
  </div>
</div>

<div id="geometricControls" style="display:none;">
  <div class="card" style="margin-top:0.6em;">
    <label for="jiggleInterval">Interval (seconds)</label>
    <input type="number" id="jiggleInterval" min="1" max="600" value="30">
    <p class="hint">Time between cursor movements. Fixed intervals are easy for detection software to identify.</p>

    <label for="jiggleDistance" style="margin-top:0.8em;">Distance (pixels): <span id="jiggleDistVal">2</span></label>
    <input type="range" id="jiggleDistance" min="1" max="50" value="2">
    <p class="hint">How far the cursor moves each step. Visible movement begins around 10 px on a typical screen.</p>

    <label style="margin-top:0.8em;"><input type="checkbox" id="jiggleRandomize"> Randomize interval (±25%)</label>
    <p class="hint">Adds slight variation to the interval. Better than fully fixed, but the distribution is still uniform and can be detected with enough samples.</p>
  </div>
</div>

<button id="jiggleSaveBtn" style="margin-top:0.6em;">Save jiggler settings</button>

<h2>Device &amp; firmware</h2>
<p class="hint">Live stats for the connected dongle. Free heap below ~80 kB may cause instability; if you see it drop that low, reboot.</p>
<p class="status" id="deviceInfoFull">Loading...</p>
<button id="rebootBtn">Reboot device</button>
<p class="hint" style="margin-top:0.3em;">Performs a clean software reboot. Settings and snippets are preserved. The Wi-Fi AP will disappear for ~8 seconds while it restarts.</p>
<p class="status" id="rebootStatus"></p>

<h3 style="margin-top:1.4em;border-top:1px solid #ddd;padding-top:0.8em;">Update firmware (OTA)</h3>
<p class="hint">Build in VS Code (PlatformIO → Build), then pick <code>.pio/build/esp32s3-usb/firmware.bin</code>. The dongle writes to the standby OTA slot and reboots — your previous firmware is kept and can be restored by re-flashing. The page reloads automatically after ~12 seconds.</p>
<form id="otaForm">
  <label for="otaFile">Firmware binary (.bin)</label>
  <input type="file" id="otaFile" accept=".bin" style="padding:0.3em 0;">
  <button type="submit" id="otaBtn" style="margin-top:0.4em;">Upload &amp; reboot</button>
</form>
<div id="otaProgressWrap" style="display:none;margin-top:0.6em;">
  <div id="otaBar" style="height:1.2em;width:0%;background:#007bff;border-radius:0.25em;transition:width 0.3s;"></div>
</div>
<p class="status" id="otaStatus"></p>

<script>
const curEl = document.getElementById('cur');
const nameEl = document.getElementById('name');
const textEl = document.getElementById('text');
const modeEl = document.getElementById('mode');
const statsEl = document.getElementById('stats');
const pasteBtn = document.getElementById('pasteBtn');
const cancelBtn = document.getElementById('cancelBtn');
const progressWrap = document.getElementById('progressWrap');
const progressBar = document.getElementById('progressBar');
const statusEl = document.getElementById('pasteStatus');

const WPM = { max: 600, fast: 120, human: 70 };
let ws = null;
let ackResolve = null;
let running = false;
let supportedChars = null; // Set populated from /api/chars
const charWarnEl = document.getElementById('charWarn');

const jiggleEnabled      = document.getElementById('jiggleEnabled');
const jiggleInterval     = document.getElementById('jiggleInterval');
const jiggleMeanInterval = document.getElementById('jiggleMeanInterval');
const jiggleDistance     = document.getElementById('jiggleDistance');
const jiggleDriftRadius  = document.getElementById('jiggleDriftRadius');
const jiggleJitter       = document.getElementById('jiggleJitter');
const jigglePattern      = document.getElementById('jigglePattern');
const jiggleRandomize    = document.getElementById('jiggleRandomize');
const jiggleSaveBtn      = document.getElementById('jiggleSaveBtn');
const jiggleStatus       = document.getElementById('jiggleStatus');

function updateJiggleMode() {
  const natural = jigglePattern.value === 'natural';
  document.getElementById('naturalControls').style.display  = natural ? '' : 'none';
  document.getElementById('geometricControls').style.display = natural ? 'none' : '';
}
jigglePattern.onchange = updateJiggleMode;

function checkUnsupportedChars() {
  if (!supportedChars) return;
  const text = textEl.value;
  let bad = 0;
  const seen = new Set();
  for (const ch of text) {
    if (ch === '\n' || ch === '\r' || ch === '\t') continue;
    if (!supportedChars.has(ch) && !seen.has(ch)) { bad++; seen.add(ch); }
  }
  if (bad > 0) {
    charWarnEl.textContent = '⚠️ ' + bad + ' character' + (bad > 1 ? 's' : '') +
      ' not supported by the ' + (layoutEl ? layoutEl.value : '') +
      ' layout — they will be skipped when typing.';
    charWarnEl.style.display = 'block';
  } else {
    charWarnEl.style.display = 'none';
  }
}

// ---- Snippet store ---------------------------------------------------------
const snippetListEl = document.getElementById('snippetList');
const saveSnippetBtn = document.getElementById('saveSnippetBtn');

function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

async function loadSnippets() {
  try {
    const list = await fetch('/api/snippets').then(r => r.json());
    if (!list.length) { snippetListEl.innerHTML = '<em>No saved snippets.</em>'; return; }
    snippetListEl.innerHTML = list.map(s =>
      `<div style="margin:0.6em 0;padding:0.5em;border:1px solid #e0e0e0;border-radius:4px;">
        <div style="display:flex;gap:0.4em;align-items:center;">
          <strong style="flex:1;">${escHtml(s.title)}</strong>
          <small style="color:#888;">${s.chars} chars</small>
          <button onclick="typeSnippet(${s.id},this)">Type</button>
          <button onclick="editSnippet(${s.id})">Edit</button>
          <button onclick="deleteSnippet(${s.id})">Delete</button>
        </div>
        ${s.preview ? `<div style="margin-top:0.3em;font-size:0.82em;color:#666;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">${escHtml(s.preview)}</div>` : ''}
      </div>`
    ).join('');
  } catch(e) { snippetListEl.innerHTML = '<em>Failed to load snippets.</em>'; }
}

async function typeSnippet(id, btn) {
  const orig = btn.textContent;
  btn.disabled = true; btn.textContent = 'Typing…';
  try {
    await fetch('/api/snippets/type?id=' + id, { method: 'POST' });
    btn.textContent = 'Sent ✓';
    setTimeout(() => { btn.textContent = orig; btn.disabled = false; }, 3000);
  } catch(e) {
    btn.textContent = orig; btn.disabled = false;
  }
}

async function deleteSnippet(id) {
  if (!confirm('Delete this snippet?')) return;
  await fetch('/api/snippets?id=' + id, { method: 'DELETE' });
  loadSnippets();
}

let _editId = -1;
const snippetEditArea = document.getElementById('snippetEditArea');
const editTitleEl = document.getElementById('editTitle');
const editTextEl  = document.getElementById('editText');

async function editSnippet(id) {
  try {
    const s = await fetch('/api/snippets?id=' + id).then(r => r.json());
    editTitleEl.value = s.title;
    editTextEl.value  = s.text;
    _editId = id;
    snippetEditArea.style.display = 'block';
    editTextEl.focus();
  } catch(e) { alert('Could not load snippet.'); }
}

document.getElementById('saveEditBtn').onclick = async () => {
  const title = editTitleEl.value.trim();
  const text  = editTextEl.value;
  if (!title) return;
  const params = new URLSearchParams({ id: _editId, title, text });
  await fetch('/api/snippets', { method: 'POST', body: params });
  snippetEditArea.style.display = 'none';
  _editId = -1;
  loadSnippets();
};

document.getElementById('cancelEditBtn').onclick = () => {
  snippetEditArea.style.display = 'none';
  _editId = -1;
};

saveSnippetBtn.onclick = async () => {
  const text = textEl.value.trim();
  if (!text) { alert('Type or paste some text first.'); return; }
  const title = prompt('Snippet name:', 'Snippet ' + Date.now());
  if (!title) return;
  const list = await fetch('/api/snippets').then(r => r.json());
  const usedIds = new Set(list.map(s => s.id));
  let slot = -1;
  for (let i = 0; i < 8; i++) { if (!usedIds.has(i)) { slot = i; break; } }
  if (slot < 0) { alert('All 8 snippet slots are full. Delete one first.'); return; }
  const params = new URLSearchParams({ id: slot, title, text });
  await fetch('/api/snippets', { method: 'POST', body: params });
  loadSnippets();
};

// ---- End snippet store ----------------------------------------------------

// ---- Human simulation -----------------------------------------------------
const simStatusEl  = document.getElementById('simStatus');
const simEnabledEl = document.getElementById('simEnabled');
const simPauseEl   = document.getElementById('simPause');
const simWordsEl   = document.getElementById('simWords');
const simSaveBtn   = document.getElementById('simSaveBtn');

async function loadSimConfig() {
  try {
    const j = await fetch('/api/sim').then(r => r.json());
    simEnabledEl.checked = j.enabled;
    simPauseEl.value = Math.round(j.pause_ms / 1000);
    simWordsEl.value = j.words_per_burst;
    simStatusEl.textContent = j.enabled
      ? 'Active — typing random words and erasing them to simulate activity.'
      : 'Inactive.';
  } catch(e) { simStatusEl.textContent = 'Failed to load.'; }
}

simSaveBtn.onclick = async () => {
  const params = new URLSearchParams({
    enabled:         simEnabledEl.checked ? '1' : '0',
    pause_ms:        String(Math.max(5, parseInt(simPauseEl.value,10) || 18) * 1000),
    words_per_burst: String(Math.max(2, Math.min(20, parseInt(simWordsEl.value,10) || 8))),
  });
  await fetch('/api/sim', { method: 'POST', body: params });
  await loadSimConfig();
};
// ---- End human simulation -------------------------------------------------

function loadSupportedChars() {
  fetch('/api/chars')
    .then(r => r.json())
    .then(j => { supportedChars = new Set(j.chars); checkUnsupportedChars(); })
    .catch(() => {});
}

function updateStats() {
  const n = textEl.value.length;
  const wpm = WPM[modeEl.value] || 120;
  let timeText;
  if (modeEl.value === 'max') {
    timeText = 'as fast as the host accepts';
  } else {
    const sec = Math.round(n * 60 / (wpm * 5));
    timeText = sec < 60 ? sec + ' s' : Math.round(sec / 60) + ' min ' + (sec % 60) + ' s';
  }
  statsEl.textContent = n + ' chars — estimated time: ' + timeText;
  checkUnsupportedChars();
}

function setUiLocked(locked) {
  pasteBtn.disabled = locked;
  cancelBtn.disabled = !locked;
  textEl.disabled = locked;
  modeEl.disabled = locked;
  if (locked) progressWrap.style.display = 'block';
}

function updateStatus(state, typed, total, pending, wpm, skipped) {
  typed   = typed   | 0;
  total   = total   | 0;
  pending = pending | 0;
  wpm     = wpm     | 0;
  skipped = skipped | 0;

  let statusText = state;
  if (state === 'typing') {
    if (total > 0) {
      const pct = Math.min(100, Math.round(typed / total * 100));
      const remaining = total - typed;
      const etaSec = wpm > 0 ? Math.round(remaining * 60 / (wpm * 5)) : 0;
      const etaStr = etaSec < 60 ? etaSec + ' s left' : Math.round(etaSec / 60) + ' min left';
      statusText = pct + '% · ' + etaStr + ' · ' + wpm + ' WPM';
      progressBar.style.width = pct + '%';
      progressBar.style.opacity = '1';
    } else {
      // Total unknown — indeterminate: pulse the bar at full width
      statusText = 'typing ' + typed + ' chars · ' + wpm + ' WPM';
      progressBar.style.width = '100%';
      progressBar.style.opacity = '0.35';
    }
  } else if (state === 'done') {
    statusText = 'Done — typed ' + typed + ' chars';
  } else if (state === 'cancelled') {
    statusText = 'Cancelled after ' + typed + ' chars';
  } else if (state === 'error') {
    statusText = 'Error after ' + typed + ' chars';
  }
  statusEl.textContent = statusText;

  if (state === 'done' || state === 'cancelled' || state === 'error') {
    if (skipped > 0) {
      statusEl.textContent += ' — ' + skipped + ' character' + (skipped > 1 ? 's' : '') +
        ' had no keymap entry and were approximated or skipped.';
    }
    running = false;
    setUiLocked(false);
    progressBar.style.width = '0%';
    progressBar.style.opacity = '1';
  }
}

function connectWs() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => {
    statusEl.textContent = 'Connected — pair the dongle with your computer, then paste.';
    ws.send(JSON.stringify({t:'jiggle_get'}));
  };
  ws.onclose = () => { statusEl.textContent = 'Disconnected. Reconnecting...'; setTimeout(connectWs, 2000); };
  ws.onmessage = (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch(e) { return; }
    if (msg.t === 'status') {
      updateStatus(msg.state, msg.typed, msg.total, msg.pending, msg.wpm, msg.skipped || 0);
    } else if (msg.t === 'ack') {
      if (ackResolve) { ackResolve(); ackResolve = null; }
    } else if (msg.t === 'jiggle_state') {
      applyJigglerState(msg);
    } else if (msg.t === 'jiggle_ack') {
      ws.send(JSON.stringify({t:'jiggle_get'}));
    }
  };
}
connectWs();

function waitAck() {
  return new Promise(resolve => { ackResolve = resolve; });
}

async function doPaste() {
  const text = textEl.value;
  if (!text) return;
  const mode = modeEl.value;
  const chunkSize = 512;
  running = true;
  setUiLocked(true);

  ws.send(JSON.stringify({t:'start', total:text.length, mode:mode}));
  for (let off = 0; off < text.length; off += chunkSize) {
    if (!running) break;
    const chunk = text.slice(off, off + chunkSize);
    ws.send(JSON.stringify({t:'chunk', text:chunk}));
    await waitAck();
  }
  if (running) statusEl.textContent = 'Finishing...';
}

pasteBtn.onclick = () => doPaste().catch(e => { statusEl.textContent = 'Error: ' + e; setUiLocked(false); });
cancelBtn.onclick = () => {
  ws.send(JSON.stringify({t:'cancel'}));
  running = false;
  if (ackResolve) { ackResolve(); ackResolve = null; }
};
textEl.oninput = updateStats;
modeEl.onchange = updateStats;

fetch('/api/config')
  .then(r => r.json())
  .then(j => {
    curEl.textContent = j.name || '(default)';
    nameEl.value = j.name || '';
  })
  .catch(e => { console.error(e); });

document.getElementById('cfgform').onsubmit = async (e) => {
  e.preventDefault();
  const name = nameEl.value.trim();
  if (!name || name.length > 31) return;
  await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'name=' + encodeURIComponent(name)
  });
  await fetch('/api/reboot', { method: 'POST' });
};

const layoutEl = document.getElementById('layout');
const layoutCurEl = document.getElementById('layoutCur');

fetch('/api/layout')
  .then(r => r.json())
  .then(j => {
    layoutEl.value = (j.layout || 'US').toUpperCase();
    layoutCurEl.textContent = layoutEl.value;
  })
  .catch(e => { console.error(e); });

document.getElementById('layoutForm').onsubmit = async (e) => {
  e.preventDefault();
  const layout = layoutEl.value.toUpperCase();
  try {
    await fetch('/api/layout', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'layout=' + encodeURIComponent(layout)
    });
    layoutCurEl.textContent = layout;
    loadSupportedChars(); // refresh char set for new layout
  } catch (e) {
    console.error(e);
  }
};

function applyJigglerState(j) {
  jiggleEnabled.checked = j.enabled;
  jigglePattern.value   = j.pattern || 'natural';
  // Natural controls
  jiggleMeanInterval.value = Math.max(5, Math.round((j.interval_ms || 30000) / 1000));
  jiggleDriftRadius.value  = j.ou_max_radius || 5;
  jiggleJitter.value       = j.ou_jitter !== undefined ? j.ou_jitter : 50;
  document.getElementById('jiggleDriftRadiusVal').textContent = jiggleDriftRadius.value;
  document.getElementById('jiggleJitterVal').textContent      = jiggleJitter.value;
  // Geometric controls
  jiggleInterval.value  = Math.max(1, Math.round((j.interval_ms || 30000) / 1000));
  jiggleDistance.value  = j.distance || 2;
  document.getElementById('jiggleDistVal').textContent = j.distance || 2;
  jiggleRandomize.checked = j.randomize || false;
  const hasMouse = j.has_mouse;
  jiggleStatus.textContent = hasMouse ? 'Mouse HID available' : 'Mouse HID not available on this backend';
  jiggleEnabled.disabled = !hasMouse;
  jiggleSaveBtn.disabled = !hasMouse;
  updateJiggleMode();
}

async function loadJigglerConfig() {
  try {
    const j = await fetch('/api/jiggler').then(r => r.json());
    applyJigglerState(j);
  } catch (e) {
    console.error(e);
    jiggleStatus.textContent = 'Failed to load jiggler config';
  }
}

jiggleSaveBtn.onclick = async () => {
  const natural = jigglePattern.value === 'natural';
  const params = new URLSearchParams();
  params.append('enabled', jiggleEnabled.checked ? '1' : '0');
  params.append('pattern', jigglePattern.value);
  if (natural) {
    params.append('interval_ms', String(Math.max(5, parseInt(jiggleMeanInterval.value, 10) || 30) * 1000));
    params.append('ou_max_radius', String(Math.max(1, Math.min(20, parseInt(jiggleDriftRadius.value, 10) || 5))));
    params.append('ou_jitter', String(Math.max(0, Math.min(100, parseInt(jiggleJitter.value, 10) || 50))));
  } else {
    params.append('interval_ms', String(Math.max(1, parseInt(jiggleInterval.value, 10) || 30) * 1000));
    params.append('distance', String(Math.max(1, Math.min(50, parseInt(jiggleDistance.value, 10) || 2))));
    params.append('randomize', jiggleRandomize.checked ? '1' : '0');
  }
  try {
    await fetch('/api/jiggler', { method: 'POST', body: params });
    jiggleStatus.textContent = 'Saved.';
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({t:'jiggle_get'}));
  } catch (e) {
    jiggleStatus.textContent = 'Save failed: ' + e;
  }
};

document.getElementById('jiggleDistance').oninput    = (e) => { document.getElementById('jiggleDistVal').textContent = e.target.value; };
document.getElementById('jiggleDriftRadius').oninput = (e) => { document.getElementById('jiggleDriftRadiusVal').textContent = e.target.value; };
document.getElementById('jiggleJitter').oninput      = (e) => { document.getElementById('jiggleJitterVal').textContent = e.target.value; };

loadJigglerConfig();

loadSupportedChars();
loadSnippets();
loadSimConfig();

fetch('/api/status')
  .then(r => r.json())
  .then(j => {
    if (j.backend === 'usb') {
      document.getElementById('bleSection').style.display = 'none';
    }
  })
  .catch(() => {});

updateStats();

// ---- Device info ----------------------------------------------------------
function fmtUptime(ms) {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return h ? `${h}h ${m}m` : m ? `${m}m ${sec}s` : `${sec}s`;
}
async function loadDeviceInfo() {
  try {
    const j = await fetch('/api/info').then(r => r.json());
    const heap = Math.round(j.free_heap / 1024);
    const minH = Math.round(j.min_free_heap / 1024);
    const bar  = `${j.backend.toUpperCase()} · v${j.firmware_version} · up ${fmtUptime(j.uptime_ms)} · heap ${heap} kB (min ${minH} kB)`;
    document.getElementById('deviceInfoBar').textContent  = bar;
    document.getElementById('deviceInfoFull').textContent = bar;
  } catch(e) {
    document.getElementById('deviceInfoFull').textContent = 'Device info unavailable (firmware pre-v0.7?)';
  }
}
loadDeviceInfo();
setInterval(loadDeviceInfo, 15000);

// ---- OTA upload -----------------------------------------------------------
const otaFileEl     = document.getElementById('otaFile');
const otaBtnEl      = document.getElementById('otaBtn');
const otaProgressWrap = document.getElementById('otaProgressWrap');
const otaBarEl      = document.getElementById('otaBar');
const otaStatusEl   = document.getElementById('otaStatus');

document.getElementById('otaForm').onsubmit = (e) => {
  e.preventDefault();
  const file = otaFileEl.files[0];
  if (!file) { otaStatusEl.textContent = 'Select a firmware .bin file first.'; return; }

  const formData = new FormData();
  formData.append('file', file, 'firmware.bin');

  const xhr = new XMLHttpRequest();
  otaBtnEl.disabled = true;
  otaProgressWrap.style.display = 'block';
  otaBarEl.style.width = '0%';
  otaStatusEl.textContent = 'Uploading...';

  xhr.upload.onprogress = (ev) => {
    if (!ev.lengthComputable) return;
    const pct = Math.round(ev.loaded / ev.total * 100);
    otaBarEl.style.width = pct + '%';
    otaStatusEl.textContent = `Uploading ${pct}%... (${Math.round(ev.loaded/1024)} / ${Math.round(ev.total/1024)} kB)`;
  };

  function startReloadCountdown(sec) {
    const tick = () => {
      otaStatusEl.textContent = `Upload complete — device rebooting. Page reloads in ${sec} s...`;
      if (sec-- > 0) setTimeout(tick, 1000); else location.reload();
    };
    tick();
  }

  xhr.onload = () => {
    otaBtnEl.disabled = false;
    if (xhr.status === 200) {
      otaBarEl.style.width = '100%';
      startReloadCountdown(12);
    } else if (xhr.status === 401) {
      otaStatusEl.textContent = 'Error 401: invalid bearer token — check serial output.';
    } else {
      otaStatusEl.textContent = `Upload failed (HTTP ${xhr.status}): ${xhr.responseText}`;
    }
  };

  xhr.onerror = () => {
    otaBtnEl.disabled = false;
    // The device reboots immediately after flashing, which drops the TCP connection
    // and fires onerror even on success. Treat as success if upload reached 100%.
    if (parseFloat(otaBarEl.style.width) >= 99) {
      startReloadCountdown(15);
    } else {
      otaStatusEl.textContent = 'Connection lost — check serial output to see if flash succeeded.';
    }
  };

  xhr.open('POST', '/api/update');
  xhr.send(formData);
};

// ---- Reboot button --------------------------------------------------------
document.getElementById('rebootBtn').onclick = async () => {
  try { await fetch('/api/reboot', { method: 'POST' }); } catch (_) {}
  let t = 8;
  const tick = () => {
    document.getElementById('rebootStatus').textContent = `Rebooting... page reloads in ${t} s`;
    if (t-- > 0) setTimeout(tick, 1000); else location.reload();
  };
  tick();
};
</script>
</body>
</html>
)rawliteral";

static void print_help() {
    Serial.println("Commands:");
    Serial.println("  name <text>    Set BLE device name (requires reset)");
    Serial.println("  getname        Show current BLE device name");
    Serial.println("  type <text>    Type text now (if connected)");
    Serial.println("  jiggle on|off  Enable/disable mouse jiggler");
    Serial.println("  jiggle cfg <interval_sec> <distance> <pattern> <random 0|1>");
    Serial.println("  jiggle status  Show mouse jiggler settings");
    Serial.println("  reboot         Restart the ESP32");
    Serial.println("  help           Show this help");
}

static void handle_serial_command(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith("name ")) {
        String name = cmd.substring(5);
        name.trim();
        if (name.length() > 0 && name.length() <= 31) {
            cfg.set_device_name(std::string(name.c_str()));
            Serial.print("Device name set to: ");
            Serial.println(name);
            Serial.println("Reset the ESP32 to advertise with the new name.");
        } else {
            Serial.println("Name must be 1-31 characters.");
        }
    } else if (cmd == "getname") {
        std::string n = cfg.get_device_name();
        Serial.print("Device name: ");
        if (n.empty()) {
            Serial.print("(default: ");
            Serial.print(DEVICE_NAME);
            Serial.println(")");
        } else {
            Serial.println(n.c_str());
        }
    } else if (cmd.startsWith("type ")) {
        String text = cmd.substring(5);
        if (backend && backend->is_connected()) {
            backend->send_string(std::string(text.c_str()));
            Serial.println("Typed.");
        } else {
            Serial.println("Not connected.");
        }
    } else if (cmd.startsWith("jiggle ")) {
        String arg = cmd.substring(7);
        arg.trim();
        arg.toLowerCase();
        if (!g_jiggler) {
            Serial.println("Jiggler not available.");
        } else if (arg == "on" || arg == "off") {
            auto js = g_jiggler->get_settings();
            js.enabled = (arg == "on");
            g_jiggler->set_settings(js);
            cfg.set_jiggler_enabled(js.enabled);
            Serial.println(js.enabled ? "Jiggler enabled." : "Jiggler disabled.");
        } else if (arg == "status") {
            auto js = g_jiggler->get_settings();
            Serial.print("Jiggler: ");
            Serial.println(js.enabled ? "on" : "off");
            Serial.print("  interval: ");
            Serial.print(js.interval_ms / 1000);
            Serial.println(" s");
            Serial.print("  distance: ");
            Serial.println(js.distance);
            Serial.print("  pattern: ");
            Serial.println(jiggler::pattern_to_string(js.pattern));
            Serial.print("  randomize: ");
            Serial.println(js.randomize_interval ? "on" : "off");
        } else if (arg.startsWith("cfg ")) {
            String rest = arg.substring(4);
            int p1 = rest.indexOf(' ');
            int p2 = p1 < 0 ? -1 : rest.indexOf(' ', p1 + 1);
            int p3 = p2 < 0 ? -1 : rest.indexOf(' ', p2 + 1);
            if (p1 > 0 && p2 > p1 && p3 > p2) {
                auto js = g_jiggler->get_settings();
                js.interval_ms = static_cast<uint32_t>(rest.substring(0, p1).toInt()) * 1000;
                js.distance = static_cast<int8_t>(rest.substring(p1 + 1, p2).toInt());
                js.pattern = jiggler::pattern_from_string(std::string(rest.substring(p2 + 1, p3).c_str()));
                js.randomize_interval = (rest.substring(p3 + 1).toInt() != 0);
                if (js.interval_ms < 1000) js.interval_ms = 1000;
                if (js.distance < 1) js.distance = 1;
                if (js.distance > 50) js.distance = 50;
                g_jiggler->set_settings(js);
                cfg.set_jiggler_interval_ms(js.interval_ms);
                cfg.set_jiggler_distance(js.distance);
                cfg.set_jiggler_pattern(jiggler::pattern_to_string(js.pattern));
                cfg.set_jiggler_randomize(js.randomize_interval);
                Serial.println("Jiggler config saved.");
            } else {
                Serial.println("Usage: jiggle cfg <interval_sec> <distance> <pattern> <random 0|1>");
            }
        } else {
            Serial.println("Usage: jiggle on|off|status|cfg ...");
        }
    } else if (cmd == "reboot") {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    } else if (cmd == "help") {
        print_help();
    } else {
        Serial.println("Unknown command. Type 'help'.");
    }
}

static const char* state_name(paster::State s) {
    switch (s) {
        case paster::State::IDLE: return "idle";
        case paster::State::TYPING: return "typing";
        case paster::State::DONE: return "done";
        case paster::State::CANCELLED: return "cancelled";
        case paster::State::ERROR: return "error";
    }
    return "unknown";
}

static void ws_broadcast_status() {
    paster::Status s = g_paster->status();
    String msg = "{\"t\":\"status\",\"state\":\"";
    msg += state_name(s.state);
    msg += "\",\"typed\":"   + String(s.chars_typed);
    msg += ",\"total\":"     + String(s.total_chars);
    msg += ",\"pending\":"   + String(s.pending);
    msg += ",\"wpm\":"       + String(s.wpm);
    msg += ",\"skipped\":"   + String(s.skipped);
    msg += "}";
    ws.textAll(msg);
}

static void ws_send(AsyncWebSocketClient* client, const String& msg) {
    if (client) client->text(msg);
}

static typing::Config mode_to_config(const String& mode) {
    if (mode == "max")   return typing::preset(typing::Mode::MAX_SPEED);
    if (mode == "human") return typing::preset(typing::Mode::HUMAN);
    return typing::preset(typing::Mode::FAST_TYPIST);
}

// Push a heap-allocated command onto the queue. Drops (and frees) if queue full.
static void push_cmd(WsCmd* c) {
    if (!g_cmd_queue || xQueueSend(g_cmd_queue, &c, 0) != pdTRUE) {
        delete c;
    }
}

static void handle_ws_message(AsyncWebSocketClient* client, const String& message) {
    // Lambdas for flat JSON extraction — avoids file-scope String return type issues.
    auto jstr = [&](const char* field) -> String {
        String needle = String("\"") + field + "\":\"";
        int p = message.indexOf(needle);
        if (p < 0) return "";
        p += needle.length();
        String out;
        while (p < (int)message.length()) {
            char ch = message[p++];
            if (ch == '"') break;
            if (ch == '\\' && p < (int)message.length()) {
                char esc = message[p++];
                out += (esc=='n') ? '\n' : (esc=='r') ? '\r' : (esc=='t') ? '\t' : esc;
                continue;
            }
            out += ch;
        }
        return out;
    };
    auto jint = [&](const char* field, int def = 0) -> int {
        String needle = String("\"") + field + "\":";
        int p = message.indexOf(needle);
        if (p < 0) return def;
        p += needle.length();
        while (p < (int)message.length() && message[p] == ' ') ++p;
        return message.substring(p).toInt();
    };
    auto jbool = [&](const char* field, bool def = false) -> bool {
        String needle = String("\"") + field + "\":";
        int p = message.indexOf(needle);
        if (p < 0) return def;
        p += needle.length();
        while (p < (int)message.length() && message[p] == ' ') ++p;
        return message[p] == 't';
    };

    String t = jstr("t");

    if (t == "start") {
        auto* c = new WsCmd{};
        c->type       = WsCmd::Type::START;
        c->client_id  = client->id();
        c->total      = jint("total", -1);
        c->typing_cfg = mode_to_config(jstr("mode"));
        push_cmd(c);
    } else if (t == "chunk") {
        auto* c = new WsCmd{};
        c->type      = WsCmd::Type::CHUNK;
        c->client_id = client->id();
        c->text      = std::string(jstr("text").c_str());
        push_cmd(c);
    } else if (t == "cancel") {
        auto* c = new WsCmd{};
        c->type = WsCmd::Type::CANCEL;
        push_cmd(c);
    } else if (t == "ping") {
        ws_send(client, "{\"t\":\"pong\"}");
    } else if (t == "jiggle") {
        auto* c = new WsCmd{};
        c->type      = WsCmd::Type::JIGGLE_ENABLE;
        c->client_id = client->id();
        c->flag      = jbool("enabled");
        push_cmd(c);
    } else if (t == "jiggle_cfg") {
        jiggler::Settings js{};
        js.interval_ms        = static_cast<uint32_t>(jint("interval_ms", 30000));
        js.distance           = static_cast<int8_t>(jint("distance", 2));
        js.pattern            = jiggler::pattern_from_string(std::string(jstr("pattern").c_str()));
        js.randomize_interval = jbool("randomize");
        if (js.interval_ms < 1000) js.interval_ms = 1000;
        if (js.distance < 1) js.distance = 1;
        if (js.distance > 50) js.distance = 50;
        auto* c = new WsCmd{};
        c->type       = WsCmd::Type::JIGGLE_CFG;
        c->client_id  = client->id();
        c->jiggle_cfg = js;
        push_cmd(c);
    } else if (t == "jiggle_get") {
        auto* c = new WsCmd{};
        c->type      = WsCmd::Type::JIGGLE_GET;
        c->client_id = client->id();
        push_cmd(c);
    }
}

// Drain the command queue and execute each command on the main-loop task.
static void drain_cmd_queue() {
    WsCmd* c = nullptr;
    while (xQueueReceive(g_cmd_queue, &c, 0) == pdTRUE) {
        switch (c->type) {
            case WsCmd::Type::START:
                if (g_paster) {
                    g_paster->set_config(c->typing_cfg);
                    g_paster->start(c->total);
                    ws_broadcast_status();
                }
                break;
            case WsCmd::Type::CHUNK:
                if (g_paster) {
                    g_paster->feed(c->text);
                    int pending = g_paster->status().pending;
                    if (pending < PASTE_HIGH_WATER) {
                        paster::Status s = g_paster->status();
                        String ack = "{\"t\":\"ack\",\"typed\":" + String(s.chars_typed)
                                   + ",\"pending\":" + String(s.pending) + "}";
                        ws.text(c->client_id, ack);
                        ws_broadcast_status();
                    } else {
                        g_ack_client_id = c->client_id;
                        g_pending_ack = true;
                    }
                }
                break;
            case WsCmd::Type::CANCEL:
                if (g_paster) {
                    g_paster->cancel();
                    g_pending_ack = false;
                    ws_broadcast_status();
                }
                break;
            case WsCmd::Type::JIGGLE_ENABLE:
                if (g_jiggler) {
                    auto js = g_jiggler->get_settings();
                    js.enabled = c->flag;
                    g_jiggler->set_settings(js);
                    cfg.set_jiggler_enabled(c->flag);
                    ws.text(c->client_id, "{\"t\":\"jiggle_ack\"}");
                }
                break;
            case WsCmd::Type::JIGGLE_CFG:
                if (g_jiggler) {
                    g_jiggler->set_settings(c->jiggle_cfg);
                    cfg.set_jiggler_interval_ms(c->jiggle_cfg.interval_ms);
                    cfg.set_jiggler_distance(c->jiggle_cfg.distance);
                    cfg.set_jiggler_pattern(jiggler::pattern_to_string(c->jiggle_cfg.pattern));
                    cfg.set_jiggler_randomize(c->jiggle_cfg.randomize_interval);
                    cfg.set_jiggler_ou_radius(c->jiggle_cfg.ou_max_radius);
                    cfg.set_jiggler_ou_jitter(c->jiggle_cfg.ou_jitter);
                    cfg.set_jiggler_ou_anim_ms(c->jiggle_cfg.ou_anim_ms);
                    ws.text(c->client_id, "{\"t\":\"jiggle_ack\"}");
                }
                break;
            case WsCmd::Type::JIGGLE_GET: {
                auto js = g_jiggler ? g_jiggler->get_settings() : jiggler::Settings{};
                String msg = "{\"t\":\"jiggle_state\"";
                msg += ",\"enabled\":"        + String(js.enabled ? "true" : "false");
                msg += ",\"interval_ms\":"    + String(js.interval_ms);
                msg += ",\"distance\":"       + String(js.distance);
                msg += ",\"pattern\":\""      + String(jiggler::pattern_to_string(js.pattern)) + "\"";
                msg += ",\"randomize\":"      + String(js.randomize_interval ? "true" : "false");
                msg += ",\"ou_max_radius\":"  + String(js.ou_max_radius);
                msg += ",\"ou_jitter\":"      + String(js.ou_jitter);
                msg += ",\"ou_anim_ms\":"     + String(js.ou_anim_ms);
                msg += ",\"has_mouse\":"      + String(backend && backend->has_mouse() ? "true" : "false");
                msg += "}";
                ws.text(c->client_id, msg);
                break;
            }
            case WsCmd::Type::MACRO_RUN:
                if (g_macro_runner) {
                    g_macro_runner->start(std::move(c->macro_cmds), cfg.get_layout());
                }
                break;
            case WsCmd::Type::MACRO_CANCEL:
                if (g_macro_runner) g_macro_runner->cancel();
                break;
        }
        delete c;
    }
}

static void on_ws_event(AsyncWebSocket* server,
                        AsyncWebSocketClient* client,
                        AwsEventType type,
                        void* arg,
                        uint8_t* data,
                        size_t len) {
    (void)server;
    if (type == WS_EVT_CONNECT) {
        ws_broadcast_status();
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            String msg;
            msg.reserve(len);
            for (size_t i = 0; i < len; ++i) msg += static_cast<char>(data[i]);
            handle_ws_message(client, msg);
        }
    }
}

static void start_web_ui() {
    String ssid = cfg.get_ap_ssid().c_str();
    String pass = cfg.get_ap_password().c_str();
    if (ssid.length() == 0) ssid = "PasteDongle";
    if (pass.length() < 8) pass = "pastepaste";

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), pass.c_str())) {
        Serial.println("[WiFi] Failed to start AP");
        return;
    }

    Serial.print("[WiFi] AP started: ");
    Serial.println(ssid);
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.softAPIP());

    ws.onEvent(on_ws_event);
    server.addHandler(&ws);

    // Serve UI from LittleFS if available, fall back to the minimal PROGMEM page.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send_P(200, "text/html", INDEX_HTML);
        }
    });

    // Upload a file to LittleFS (used to push index.html without a firmware reflash).
    // curl: curl -X POST http://192.168.4.1/api/fs/upload -F "file=@data/index.html"
    server.on("/api/fs/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (LittleFS.totalBytes() == 0) {
                request->send(503, "application/json",
                              "{\"ok\":false,\"error\":\"filesystem not mounted\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true}");
        },
        [](AsyncWebServerRequest* /*req*/, const String& filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            static File upload_file;
            if (!index) {
                String path = "/" + filename;
                Serial.printf("[FS] Upload start: %s (FS total=%u)\n",
                              path.c_str(), LittleFS.totalBytes());
                upload_file = LittleFS.open(path, "w");
                if (!upload_file) Serial.println("[FS] ERROR: open for write failed");
            }
            if (upload_file) upload_file.write(data, len);
            if (final) {
                if (upload_file) {
                    upload_file.close();
                    Serial.printf("[FS] Upload done: /%s (%u bytes), FS used=%u\n",
                                  filename.c_str(), index + len, LittleFS.usedBytes());
                } else {
                    Serial.println("[FS] Upload failed: file was not open");
                }
            }
        }
    );

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{\"name\":\"";
        body += cfg.get_device_name().c_str();
        body += "\"}";
        request->send(200, "application/json", body);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name", true)) {
            request->send(400, "text/plain", "missing name");
            return;
        }
        String name = request->getParam("name", true)->value();
        name.trim();
        if (name.length() == 0 || name.length() > 31) {
            request->send(400, "text/plain", "name must be 1-31 characters");
            return;
        }
        cfg.set_device_name(std::string(name.c_str()));
        request->send(200, "text/plain", "saved");
    });

    server.on("/api/layout", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{\"layout\":\"";
        body += cfg.get_layout().c_str();
        body += "\"}";
        request->send(200, "application/json", body);
    });

    server.on("/api/layout", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("layout", true)) {
            request->send(400, "text/plain", "missing layout");
            return;
        }
        String layout = request->getParam("layout", true)->value();
        layout.trim();
        layout.toUpperCase();
        if (!keymap::is_supported(std::string(layout.c_str()))) {
            request->send(400, "text/plain", "unsupported layout");
            return;
        }
        cfg.set_layout(std::string(layout.c_str()));
        if (backend) backend->set_layout(std::string(layout.c_str()));
        request->send(200, "text/plain", "saved");
    });

    // Return all typeable characters for the current layout.
    // The web UI uses this to warn about unsupported characters before pasting.
    server.on("/api/chars", HTTP_GET, [](AsyncWebServerRequest* request) {
        std::string chars = keymap::supported_chars(cfg.get_layout());
        String body = "{\"layout\":\"";
        body += cfg.get_layout().c_str();
        body += "\",\"chars\":\"";
        // Escape the string for safe JSON embedding.
        for (char c : chars) {
            if (c == '"')       body += "\\\"";
            else if (c == '\\') body += "\\\\";
            else                body += c;
        }
        body += "\"}";
        request->send(200, "application/json", body);
    });

    // Run a macro script POSTed as form field "script".
    server.on("/api/macro/run", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("script", true)) {
            request->send(400, "text/plain", "missing script");
            return;
        }
        String script = request->getParam("script", true)->value();
        auto cmds = macro::parse(std::string(script.c_str()));
        auto* c = new WsCmd{};
        c->type = WsCmd::Type::MACRO_RUN;
        c->macro_cmds = std::move(cmds);
        push_cmd(c);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/macro/cancel", HTTP_POST, [](AsyncWebServerRequest* request) {
        auto* c = new WsCmd{};
        c->type = WsCmd::Type::MACRO_CANCEL;
        push_cmd(c);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // ------ Human simulation -----------------------------------------------
    server.on("/api/sim", HTTP_GET, [](AsyncWebServerRequest* request) {
        sim::HumanSim::Config cfg = g_sim ? g_sim->get_config() : sim::HumanSim::Config{};
        String body = "{";
        body += "\"enabled\":"   + String(cfg.enabled ? "true" : "false");
        body += ",\"char_delay_ms\":" + String(cfg.char_delay_ms);
        body += ",\"pause_ms\":"      + String(cfg.pause_ms);
        body += ",\"words_per_burst\":" + String(cfg.words_per_burst);
        body += "}";
        request->send(200, "application/json", body);
    });

    server.on("/api/sim", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!g_sim) { request->send(500, "text/plain", "sim not initialized"); return; }
        sim::HumanSim::Config sc = g_sim->get_config();
        if (request->hasParam("enabled", true)) {
            String v = request->getParam("enabled", true)->value();
            sc.enabled = (v == "1" || v == "true");
        }
        if (request->hasParam("char_delay_ms", true))
            sc.char_delay_ms = static_cast<uint32_t>(
                request->getParam("char_delay_ms", true)->value().toInt());
        if (request->hasParam("pause_ms", true))
            sc.pause_ms = static_cast<uint32_t>(
                request->getParam("pause_ms", true)->value().toInt());
        if (request->hasParam("words_per_burst", true))
            sc.words_per_burst = static_cast<uint8_t>(
                request->getParam("words_per_burst", true)->value().toInt());
        g_sim->set_config(sc);
        cfg.set_sim_enabled(sc.enabled);
        cfg.set_sim_pause_ms(sc.pause_ms);
        cfg.set_sim_words_burst(sc.words_per_burst);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // ------ Snippet store endpoints ----------------------------------------

    // GET /api/snippets          → JSON array of occupied slots (no text body)
    // GET /api/snippets?id=N     → Full snippet including text
    server.on("/api/snippets", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (request->hasParam("id")) {
            int id = request->getParam("id")->value().toInt();
            snippet::Snippet s = snippets.get(id);
            if (s.id < 0) { request->send(404, "text/plain", "not found"); return; }
            String body = "{\"id\":" + String(s.id);
            body += ",\"title\":\"";
            for (char c : s.title) { if (c=='"') body+="\\\""; else body+=c; }
            body += "\",\"text\":\"";
            for (char c : s.text)  { if (c=='"') body+="\\\""; else if(c=='\\') body+="\\\\"; else if(c=='\n') body+="\\n"; else body+=c; }
            body += "\"}";
            request->send(200, "application/json", body);
        } else {
            auto list = snippets.list();
            String body = "[";
            for (size_t i = 0; i < list.size(); ++i) {
                if (i) body += ",";
                const auto& s = list[i];
                body += "{\"id\":" + String(s.id);
                body += ",\"title\":\"";
                for (char c : s.title) { if (c=='"') body+="\\\""; else body+=c; }
                body += "\",\"chars\":" + String(s.text.size());
                body += ",\"preview\":\"";
                {
                    size_t plen = std::min(s.text.size(), size_t(72));
                    for (size_t pi = 0; pi < plen; ++pi) {
                        char pc = s.text[pi];
                        if (pc == '"')       body += "\\\"";
                        else if (pc == '\\') body += "\\\\";
                        else if (pc == '\n' || pc == '\r') body += ' ';
                        else                 body += pc;
                    }
                    if (s.text.size() > 72) body += "...";
                }
                body += "\"}";
            }
            body += "]";
            request->send(200, "application/json", body);
        }
    });

    // POST /api/snippets/type — must be registered BEFORE /api/snippets POST
    // because ESPAsyncWebServer does prefix matching and /api/snippets would
    // swallow /api/snippets/type otherwise.
    server.on("/api/snippets/type", HTTP_POST, [](AsyncWebServerRequest* request) {
        int id = -1;
        if (request->hasParam("id", true))
            id = request->getParam("id", true)->value().toInt();
        else if (request->hasParam("id"))
            id = request->getParam("id")->value().toInt();
        if (id < 0) { request->send(400, "text/plain", "missing id"); return; }
        snippet::Snippet s = snippets.get(id);
        if (s.id < 0) { request->send(404, "text/plain", "not found"); return; }
        auto* cs = new WsCmd{}; cs->type = WsCmd::Type::START;
        cs->total = static_cast<int>(s.text.size());
        cs->typing_cfg = typing::preset(typing::Mode::FAST_TYPIST);
        push_cmd(cs);
        auto* cc = new WsCmd{}; cc->type = WsCmd::Type::CHUNK;
        cc->text = s.text;
        push_cmd(cc);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/snippets with id=N&title=...&text=...  → save/overwrite
    server.on("/api/snippets", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("id", true) || !request->hasParam("text", true)) {
            request->send(400, "text/plain", "missing id or text");
            return;
        }
        int id = request->getParam("id", true)->value().toInt();
        String title = request->hasParam("title", true)
                       ? request->getParam("title", true)->value()
                       : String("Snippet ") + String(id);
        String text  = request->getParam("text", true)->value();
        if (!snippets.set(id, std::string(title.c_str()), std::string(text.c_str()))) {
            request->send(400, "text/plain", "invalid id");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // DELETE /api/snippets?id=N  → clear slot
    server.on("/api/snippets", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("id")) { request->send(400, "text/plain", "missing id"); return; }
        snippets.erase(request->getParam("id")->value().toInt());
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/fs/info", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{";
        body += "\"mounted\":" + String(LittleFS.totalBytes() > 0 ? "true" : "false");
        body += ",\"index_html\":" + String(LittleFS.exists("/index.html") ? "true" : "false");
        body += ",\"total\":" + String(LittleFS.totalBytes());
        body += ",\"used\":"  + String(LittleFS.usedBytes());
        File root = LittleFS.open("/");
        body += ",\"files\":[";
        bool first = true;
        if (root) {
            File f = root.openNextFile();
            while (f) {
                if (!first) body += ",";
                body += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
                first = false;
                f = root.openNextFile();
            }
        }
        body += "]}";
        request->send(200, "application/json", body);
    });

    server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{";
        body += "\"uptime_ms\":"    + String(millis());
        body += ",\"free_heap\":"   + String(ESP.getFreeHeap());
        body += ",\"min_free_heap\":" + String(ESP.getMinFreeHeap());
        body += ",\"heap_size\":"   + String(ESP.getHeapSize());
        body += ",\"firmware_version\":\"" PASTE_DONGLE_VERSION "\"";
#ifdef HID_BACKEND_USB
        body += (g_rtc_usb_mode & 1) ? ",\"hid_mode\":\"mouse_only\"" : ",\"hid_mode\":\"composite\"";
        body += (g_rtc_usb_mode & 2) ? ",\"serial_enabled\":true" : ",\"serial_enabled\":false";
        body += ",\"backend\":\"usb\"";
#else
        body += ",\"hid_mode\":\"composite\",\"serial_enabled\":false,\"backend\":\"ble\"";
#endif
        body += "}";
        request->send(200, "application/json", body);
    });

    // GET: returns current USB mode flags
    // POST: mouse_only=0|1  serial=0|1  → saves to NVS, sets RTC, reboots
    server.on("/api/hid_mode", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{";
#ifdef HID_BACKEND_USB
        body += "\"mouse_only\":"     + String((g_rtc_usb_mode & 1) ? "true" : "false");
        body += ",\"serial_enabled\":" + String((g_rtc_usb_mode & 2) ? "true" : "false");
#else
        body += "\"mouse_only\":false,\"serial_enabled\":false";
#endif
        body += "}";
        request->send(200, "application/json", body);
    });

    server.on("/api/hid_mode", HTTP_POST, [](AsyncWebServerRequest* request) {
#ifdef HID_BACKEND_USB
        bool changed = false;
        bool mouse_only = cfg.get_hid_mouse_only();
        bool serial_on  = cfg.get_hid_serial_enabled();
        if (request->hasParam("mouse_only", true)) {
            String v = request->getParam("mouse_only", true)->value();
            mouse_only = (v == "1" || v == "true");
            changed = true;
        }
        if (request->hasParam("serial", true)) {
            String v = request->getParam("serial", true)->value();
            serial_on = (v == "1" || v == "true");
            changed = true;
        }
        if (changed) {
            cfg.set_hid_mouse_only(mouse_only);
            cfg.set_hid_serial_enabled(serial_on);
            // Update g_rtc_usb_mode so the runtime behaviour changes immediately
            // without a reboot (e.g. keyboard suppression takes effect right away).
            g_rtc_usb_mode = (mouse_only ? 1 : 0) | (serial_on ? 2 : 0);
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
#endif
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/hid_identity", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = "{";
        body += "\"vid\":"  + String(cfg.get_usb_vid());
        body += ",\"pid\":" + String(cfg.get_usb_pid());
        body += ",\"manufacturer\":\"";
        for (char c : cfg.get_usb_manufacturer()) { if (c=='"') body+="\\\""; else body+=c; }
        body += "\",\"product\":\"";
        for (char c : cfg.get_usb_product()) { if (c=='"') body+="\\\""; else body+=c; }
        body += "\"}";
        request->send(200, "application/json", body);
    });

    server.on("/api/hid_identity", HTTP_POST, [](AsyncWebServerRequest* request) {
        bool changed = false;
        if (request->hasParam("vid", true)) {
            uint32_t v = static_cast<uint32_t>(strtoul(request->getParam("vid", true)->value().c_str(), nullptr, 16));
            if (v > 0 && v <= 0xFFFF) { cfg.set_usb_vid(v); changed = true; }
        }
        if (request->hasParam("pid", true)) {
            uint32_t v = static_cast<uint32_t>(strtoul(request->getParam("pid", true)->value().c_str(), nullptr, 16));
            if (v <= 0xFFFF) { cfg.set_usb_pid(v); changed = true; }
        }
        if (request->hasParam("manufacturer", true)) {
            String v = request->getParam("manufacturer", true)->value();
            v.trim(); if (v.length() > 0) { cfg.set_usb_manufacturer(std::string(v.c_str())); changed = true; }
        }
        if (request->hasParam("product", true)) {
            String v = request->getParam("product", true)->value();
            v.trim(); if (v.length() > 0) { cfg.set_usb_product(std::string(v.c_str())); changed = true; }
        }
        (void)changed;
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
#ifdef HID_BACKEND_USB
        const char* backend_type = "usb";
#else
        const char* backend_type = "ble";
#endif
        String body = "{\"backend\":\"";
        body += backend_type;
        body += "\",\"connected\":";
        body += (backend && backend->is_connected()) ? "true" : "false";
        body += "}";
        request->send(200, "application/json", body);
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "rebooting");
        delay(100);
        ESP.restart();
    });

    server.on("/api/jiggler", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto js = g_jiggler ? g_jiggler->get_settings() : jiggler::Settings{};
        String body = "{";
        body += "\"enabled\":"      + String(js.enabled ? "true" : "false");
        body += ",\"interval_ms\":" + String(js.interval_ms);
        body += ",\"distance\":"    + String(js.distance);
        body += ",\"pattern\":\""   + String(jiggler::pattern_to_string(js.pattern)) + "\"";
        body += ",\"randomize\":"   + String(js.randomize_interval ? "true" : "false");
        body += ",\"ou_max_radius\":" + String(js.ou_max_radius);
        body += ",\"ou_jitter\":"    + String(js.ou_jitter);
        body += ",\"ou_anim_ms\":"   + String(js.ou_anim_ms);
        body += ",\"has_mouse\":"    + String(backend && backend->has_mouse() ? "true" : "false");
        body += "}";
        request->send(200, "application/json", body);
    });

    server.on("/api/jiggler", HTTP_POST, [](AsyncWebServerRequest* request) {
        // Read current settings from the config cache (written only by main loop —
        // safe to read here). Build updated settings, push through command queue so
        // the mutation happens on the main-loop task and avoids a data race with tick().
        jiggler::Settings js{};
        js.enabled           = cfg.get_jiggler_enabled();
        js.interval_ms       = cfg.get_jiggler_interval_ms();
        js.distance          = cfg.get_jiggler_distance();
        js.pattern           = jiggler::pattern_from_string(cfg.get_jiggler_pattern());
        js.randomize_interval = cfg.get_jiggler_randomize();

        bool changed = false;
        if (request->hasParam("enabled", true)) {
            String v = request->getParam("enabled", true)->value();
            js.enabled = (v == "1" || v == "true");
            changed = true;
        }
        if (request->hasParam("interval_ms", true)) {
            int iv = request->getParam("interval_ms", true)->value().toInt();
            if (iv < 1000) iv = 1000;
            js.interval_ms = static_cast<uint32_t>(iv);
            changed = true;
        }
        if (request->hasParam("distance", true)) {
            int d = request->getParam("distance", true)->value().toInt();
            if (d < 1) d = 1;
            if (d > 50) d = 50;
            js.distance = static_cast<int8_t>(d);
            changed = true;
        }
        if (request->hasParam("pattern", true)) {
            js.pattern = jiggler::pattern_from_string(
                std::string(request->getParam("pattern", true)->value().c_str()));
            changed = true;
        }
        if (request->hasParam("randomize", true)) {
            String v = request->getParam("randomize", true)->value();
            js.randomize_interval = (v == "1" || v == "true");
            changed = true;
        }
        if (request->hasParam("ou_max_radius", true)) {
            int v = request->getParam("ou_max_radius", true)->value().toInt();
            if (v < 1) v = 1; if (v > 20) v = 20;
            js.ou_max_radius = static_cast<uint8_t>(v);
            changed = true;
        }
        if (request->hasParam("ou_jitter", true)) {
            int v = request->getParam("ou_jitter", true)->value().toInt();
            if (v < 0) v = 0; if (v > 100) v = 100;
            js.ou_jitter = static_cast<uint8_t>(v);
            changed = true;
        }
        if (request->hasParam("ou_anim_ms", true)) {
            int v = request->getParam("ou_anim_ms", true)->value().toInt();
            if (v < 50) v = 50; if (v > 800) v = 800;
            js.ou_anim_ms = static_cast<uint16_t>(v);
            changed = true;
        }
        if (changed) {
            auto* c = new WsCmd{};
            c->type = WsCmd::Type::JIGGLE_CFG;
            c->jiggle_cfg = js;
            push_cmd(c);
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // OTA firmware update endpoint. AsyncWebServer only invokes the upload
    // handler for multipart/form-data file uploads, so clients must send the
    // firmware as a multipart file (see scripts/ota_upload.py).
    server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            bool* ok_ptr = static_cast<bool*>(request->_tempObject);
            bool ok = ok_ptr ? *ok_ptr : false;
            delete ok_ptr;
            request->_tempObject = nullptr;
            if (ok) {
                request->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
                Serial.println("[OTA] Rebooting to apply update");
                delay(100);
                ESP.restart();
            } else {
                request->send(500, "application/json", "{\"ok\":false,\"error\":\"invalid update\"}");
            }
        },
        [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                bool* ok_ptr = new bool(false);
                request->_tempObject = ok_ptr;
                size_t total = request->contentLength();
                Serial.printf("[OTA] Start update: filename='%s' contentLength=%u first_byte=0x%02X\n",
                              filename.c_str(), total, data && len ? data[0] : 0xFF);
                if (total > 0 && total < 8192) {
                    Serial.printf("[OTA] Rejected: contentLength=%u < 8192\n", total);
                    Update.abort();
                    return;
                }
                size_t begin_size = (total > 0) ? total : UPDATE_SIZE_UNKNOWN;
                if (!Update.begin(begin_size)) {
                    Update.printError(Serial);
                } else {
                    Serial.printf("[OTA] Update.begin(%u) ok, freeHeap=%u\n", begin_size, ESP.getFreeHeap());
                }
            }

            if (Update.hasError()) {
                return;
            }

            if (len > 0) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                    Update.abort();
                    return;
                }
                if ((index / 32768) != ((index + len) / 32768) || final) {
                    Serial.printf("[OTA] Progress: %u bytes written\n", index + len);
                }
            }

            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Update success: %u bytes\n", index + len);
                    bool* ok_ptr = static_cast<bool*>(request->_tempObject);
                    if (ok_ptr) *ok_ptr = true;
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    // Captive portal HTTP handlers — phones do a connectivity check when joining a
    // WiFi network. If the expected response isn't returned, the OS shows a
    // "Sign in to network" popup that opens our UI automatically.
    // DNS already redirects all queries to us (see below), so every HTTP request
    // for any host arrives here. We handle the known check URLs explicitly and
    // catch everything else with onNotFound.
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* r) {
        // iOS checks captive.apple.com/hotspot-detect.html — expects body "Success"
        r->send(200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>"
            "<script>window.location='http://192.168.4.1/'</script>"
            "Success</BODY></HTML>");
    });
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* r) {
        // Android/Chrome checks connectivitycheck.gstatic.com/generate_204
        r->redirect("http://192.168.4.1/");
    });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* r) {
        // Windows checks msftconnecttest.com/connecttest.txt
        r->redirect("http://192.168.4.1/");
    });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* r) {
        // Windows NCSI check
        r->redirect("http://192.168.4.1/");
    });
    server.onNotFound([](AsyncWebServerRequest* r) {
        // Catch-all: any other URL (from any captured DNS query) → redirect to UI
        r->redirect("http://192.168.4.1/");
    });

    server.begin();
    // Captive portal: redirect every DNS query to us so phones auto-open the UI.
    dns_server.start(53, "*", WiFi.softAPIP());

    if (MDNS.begin("paste")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] paste.local registered");
    }
    Serial.println("[Web] UI ready at http://" + WiFi.softAPIP().toString() + "  (also http://paste.local)");
}

void setup() {
    Serial.begin(115200);

    delay(1000);
    Serial.println("Starting ESP32 Paste Dongle");

    // Explicitly name the partition ("littlefs" from partitions_4mb.csv).
    // Without the label, ESP-IDF searches by subtype which can fail on some
    // framework versions when board_build.filesystem=littlefs is set.
    bool fs_ok = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    if (!fs_ok) {
        Serial.println("[FS] Mount failed — formatting LittleFS partition...");
        LittleFS.format();
        fs_ok = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    }
    if (fs_ok) {
        Serial.printf("[FS] LittleFS mounted: %u / %u bytes used, index.html=%s\n",
                      LittleFS.usedBytes(), LittleFS.totalBytes(),
                      LittleFS.exists("/index.html") ? "yes" : "no");
    } else {
        Serial.println("[FS] LittleFS mount failed even after format — filesystem unavailable");
    }

#ifndef HID_BACKEND_USB
    cfg.begin();
#endif
#ifdef HID_BACKEND_USB
    // Set runtime mode flags from NVS so behaviour (keyboard suppression etc.)
    // is correct without needing a reboot or RTC tricks.
    g_rtc_usb_mode = (cfg.get_hid_mouse_only()     ? 1 : 0)
                   | (cfg.get_hid_serial_enabled() ? 2 : 0);
    Serial.printf("[USB] mode=0x%02X (mouse_only=%d serial=%d)\n",
                  g_rtc_usb_mode, (g_rtc_usb_mode&1)!=0, (g_rtc_usb_mode&2)!=0);
#endif
    if (cfg.get_device_name().empty()) {
        cfg.set_device_name(DEVICE_NAME);
    }
    snippets.begin();
    randomSeed(esp_random());

    Serial.print("Device name: ");
    Serial.println(cfg.get_device_name().c_str());
    print_help();

    backend = hid::create_backend();
#ifdef HID_BACKEND_USB
    static_cast<hid::UsbHidBackend*>(backend)->set_identity(
        static_cast<uint16_t>(cfg.get_usb_vid()),
        static_cast<uint16_t>(cfg.get_usb_pid()),
        cfg.get_usb_manufacturer(),
        cfg.get_usb_product());
#endif
    if (!backend || !backend->begin()) {
        Serial.println("Failed to start HID backend");
        return;
    }
    backend->set_layout(cfg.get_layout());
    Serial.printf("[Layout] host layout: %s\n", cfg.get_layout().c_str());

    g_paster = new paster::Paster(backend, typing::preset(typing::Mode::FAST_TYPIST));
    g_macro_runner = new macro::Runner(backend);
    g_sim = new sim::HumanSim(backend);
    {
        sim::HumanSim::Config sc;
        sc.enabled         = cfg.get_sim_enabled();
        sc.pause_ms        = cfg.get_sim_pause_ms();
        sc.words_per_burst = cfg.get_sim_words_burst();
        g_sim->set_config(sc);
    }
    g_led.begin();

    jiggler::Settings js;
    js.enabled            = cfg.get_jiggler_enabled();
    js.interval_ms        = cfg.get_jiggler_interval_ms();
    js.distance           = cfg.get_jiggler_distance();
    js.pattern            = jiggler::pattern_from_string(cfg.get_jiggler_pattern());
    js.randomize_interval = cfg.get_jiggler_randomize();
    js.ou_max_radius      = cfg.get_jiggler_ou_radius();
    js.ou_jitter          = cfg.get_jiggler_ou_jitter();
    js.ou_anim_ms         = cfg.get_jiggler_ou_anim_ms();
    g_jiggler = new jiggler::Jiggler(backend);
    g_jiggler->set_settings(js);
    Serial.printf("[Jiggler] enabled=%d interval=%ums distance=%d pattern=%s random=%d\n",
                  js.enabled, js.interval_ms, js.distance,
                  jiggler::pattern_to_string(js.pattern), js.randomize_interval);

    g_cmd_queue = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(WsCmd*));

    Serial.println("Backend started. Waiting for host connection...");
    start_web_ui();
}

void loop() {
    if (!backend) return;
    dns_server.processNextRequest();

    while (Serial.available()) {
        static String serial_buf;
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (serial_buf.length() > 0) {
                handle_serial_command(serial_buf);
                serial_buf = "";
            }
        } else {
            serial_buf += c;
        }
    }

    bool connected = backend->is_connected();
    if (connected && !was_connected) {
        Serial.println("Host connected.");
    }
    was_connected = connected;

    drain_cmd_queue();

    // Confirm the running firmware is valid once we've been up for 60 s.
    // If this is never called after an OTA update and the device resets before
    // 60 s, the bootloader will roll back to the previous slot automatically
    // (requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in sdkconfig).
    static bool ota_confirmed = false;
    if (!ota_confirmed && millis() > 60000) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        ota_confirmed = true;
        if (err == ESP_OK) {
            Serial.println("[OTA] Firmware confirmed valid (60 s uptime)");
        }
        // ESP_ERR_INVALID_STATE means rollback is not enabled — harmless.
    }

    if (g_macro_runner) {
        g_macro_runner->tick();
    }

    // Human sim only runs when paster and macro runner are both idle.
    if (g_sim) {
        bool paster_idle = !g_paster || g_paster->status().state != paster::State::TYPING;
        bool macro_idle  = !g_macro_runner || !g_macro_runner->is_running();
        if (paster_idle && macro_idle) g_sim->tick();
    }

    if (g_jiggler) {
        g_jiggler->tick();
    }

    if (g_paster) {
        g_paster->tick();

        // Send a deferred chunk ACK once the paste buffer drains enough.
        if (g_pending_ack && g_paster->status().pending < PASTE_LOW_WATER) {
            paster::Status s = g_paster->status();
            String ack = "{\"t\":\"ack\",\"typed\":" + String(s.chars_typed)
                       + ",\"pending\":" + String(s.pending) + "}";
            ws.text(g_ack_client_id, ack);
            g_pending_ack = false;
            g_ack_client_id = 0;
        }

        unsigned long now = millis();
        paster::Status s = g_paster->status();
        bool state_changed = (s.state != last_reported_state);
        bool progress_changed = (s.chars_typed != last_reported_typed);
        if (state_changed || (progress_changed && now - last_status_ms >= 250)) {
            ws_broadcast_status();
            last_reported_state = s.state;
            last_reported_typed = s.chars_typed;
            last_status_ms = now;
        }
    }

    // Update LED to reflect the current device state.
    {
        bool connected = backend->is_connected();
        bool typing    = g_paster && g_paster->status().state == paster::State::TYPING;
        bool macroing  = g_macro_runner && g_macro_runner->is_running();
        bool simming   = g_sim && g_sim->is_enabled();
        bool jiggling  = g_jiggler && g_jiggler->is_enabled();

        if (!connected)  g_led.set_state(led::State::NO_HOST);
        else if (typing) g_led.set_state(led::State::TYPING);
        else if (macroing) g_led.set_state(led::State::MACRO);
        else if (simming)  g_led.set_state(led::State::SIMULATING);
        else if (jiggling) g_led.set_state(led::State::JIGGLING);
        else               g_led.set_state(led::State::IDLE);
    }
    g_led.tick();

    delay(5);
}
