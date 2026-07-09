// ESP32 paste dongle: BLE or USB HID keyboard controlled via WiFi AP + web UI.
// Compile with HID_BACKEND_BLE for classic ESP32, or HID_BACKEND_USB for ESP32-S3.

#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "hid/ihid_backend.h"
#include "keymap.h"
#include "config_store.h"
#include "typing_engine.h"
#include "paster.h"
#include "mouse_jiggler.h"

static hid::IHidBackend* backend = nullptr;
static bool was_connected = false;
static config::Store cfg;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static paster::Paster* g_paster = nullptr;
static jiggler::Jiggler* g_jiggler = nullptr;


static unsigned long last_status_ms = 0;
static int last_reported_typed = -1;
static paster::State last_reported_state = paster::State::IDLE;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Paste Dongle</title>
<style>
body { font-family: system-ui, sans-serif; max-width: 640px; margin: 2em auto; padding: 0 1em; }
h2 { margin-top: 1.8em; border-bottom: 1px solid #ddd; padding-bottom: 0.2em; }
label { display: block; margin: 1em 0 0.25em; }
input[type="text"], select, textarea { width: 100%; padding: 0.5em; box-sizing: border-box; font: inherit; }
textarea { resize: vertical; }
button { padding: 0.6em 1.2em; margin: 0.5em 0.5em 0 0; }
#progressWrap { width: 100%; background: #e9ecef; border-radius: 0.25em; overflow: hidden; }
#progressBar { width: 0%; height: 1.2em; background: #007bff; transition: width 0.2s; }
.status { color: #555; margin-top: 0.5em; }
#stats { font-size: 0.9em; color: #666; }
</style>
</head>
<body>
<h1>ESP32 Paste Dongle</h1>

<h2>Bluetooth name</h2>
<p>Current BLE name: <strong id="cur">...</strong></p>
<form id="cfgform">
  <label for="name">Advertise as</label>
  <input type="text" id="name" name="name" maxlength="31" placeholder="e.g. Logitech K380">
  <button type="submit">Save &amp; Reboot</button>
</form>

<h2>Keyboard layout</h2>
<p>Current layout: <strong id="layoutCur">US</strong></p>
<form id="layoutForm">
  <label for="layout">Host keyboard layout</label>
  <select id="layout">
    <option value="US">US QWERTY</option>
    <option value="DVORAK">Dvorak</option>
  </select>
  <button type="submit">Save layout</button>
</form>

<h2>Paste into host computer</h2>
<label for="mode">Speed mode</label>
<select id="mode">
  <option value="max">Max Speed</option>
  <option value="fast" selected>Fast Typist (~120 WPM)</option>
  <option value="human">Human / Careful (~70 WPM)</option>
</select>

<label for="text">Text to type</label>
<textarea id="text" rows="10" placeholder="Paste your text here..."></textarea>
<p id="stats">0 chars — estimated time: -</p>

<button id="pasteBtn">Paste it</button>
<button id="cancelBtn" disabled>Cancel</button>

<div id="progressWrap" style="display:none; margin-top:1em;">
  <div id="progressBar"></div>
</div>
<p class="status" id="pasteStatus">Connecting...</p>

<h2>Mouse jiggler</h2>
<p id="jiggleStatus" class="status">Loading...</p>
<label><input type="checkbox" id="jiggleEnabled"> Enable mouse jiggler</label>
<label for="jiggleInterval">Interval (seconds)</label>
<input type="number" id="jiggleInterval" min="1" max="600" value="30">
<label for="jiggleDistance">Distance (pixels)</label>
<input type="range" id="jiggleDistance" min="1" max="20" value="2">
<label for="jigglePattern">Pattern</label>
<select id="jigglePattern">
  <option value="random">Random</option>
  <option value="line">Line</option>
  <option value="square">Square</option>
  <option value="circle">Circle</option>
</select>
<label><input type="checkbox" id="jiggleRandomize"> Randomize interval (+/- 25%)</label>
<button id="jiggleSaveBtn">Save jiggler settings</button>

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

const jiggleEnabled = document.getElementById('jiggleEnabled');
const jiggleInterval = document.getElementById('jiggleInterval');
const jiggleDistance = document.getElementById('jiggleDistance');
const jigglePattern = document.getElementById('jigglePattern');
const jiggleRandomize = document.getElementById('jiggleRandomize');
const jiggleSaveBtn = document.getElementById('jiggleSaveBtn');
const jiggleStatus = document.getElementById('jiggleStatus');

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
}

function setUiLocked(locked) {
  pasteBtn.disabled = locked;
  cancelBtn.disabled = !locked;
  textEl.disabled = locked;
  modeEl.disabled = locked;
  if (locked) progressWrap.style.display = 'block';
}

function updateStatus(state, typed, total, pending, wpm) {
  typed = parseInt(typed, 10);
  total = parseInt(total, 10);
  pending = parseInt(pending, 10);
  wpm = parseInt(wpm, 10);
  statusEl.textContent = state + ' | typed ' + typed + ' | pending ' + pending + ' | ' + wpm + ' WPM';
  if (total > 0) {
    progressBar.style.width = Math.min(100, Math.round(typed / total * 100)) + '%';
  }
  if (state === 'done' || state === 'cancelled' || state === 'error') {
    running = false;
    setUiLocked(false);
    progressBar.style.width = '0%';
  }
}

function connectWs() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => { statusEl.textContent = 'Connected — pair the dongle with your computer, then paste.'; };
  ws.onclose = () => { statusEl.textContent = 'Disconnected. Reconnecting...'; setTimeout(connectWs, 2000); };
  ws.onmessage = (ev) => {
    const parts = ev.data.split('|');
    const evName = parts[0];
    if (evName === 'status') {
      updateStatus(parts[1], parts[2], parts[3], parts[4], parts[5]);
    } else if (evName === 'ack') {
      if (ackResolve) { ackResolve(); ackResolve = null; }
    } else if (evName === 'done' || evName === 'cancelled' || evName === 'error') {
      updateStatus(evName, parts[1] || '0', parts[2] || '0', '0', parts[3] || '0');
    } else if (evName === 'jiggle_state') {
      jiggleEnabled.checked = parts[1] === '1';
      jiggleInterval.value = Math.max(1, Math.round((parts[2] || 30000) / 1000));
      jiggleDistance.value = parts[3] || 2;
      jigglePattern.value = parts[4] || 'random';
      jiggleRandomize.checked = parts[5] === '1';
      const hasMouse = parts[6] === '1';
      jiggleStatus.textContent = hasMouse ? 'Mouse HID available' : 'Mouse HID not available on this backend';
      jiggleEnabled.disabled = !hasMouse;
      jiggleSaveBtn.disabled = !hasMouse;
    } else if (evName === 'jiggle_ack') {
      ws.send('jiggle_get');
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

  ws.send('start|' + text.length + '|' + mode);
  for (let off = 0; off < text.length; off += chunkSize) {
    if (!running) break;
    const chunk = text.slice(off, off + chunkSize);
    ws.send('chunk|' + chunk);
    await waitAck();
  }
  if (running) statusEl.textContent = 'Finishing...';
}

pasteBtn.onclick = () => doPaste().catch(e => { statusEl.textContent = 'Error: ' + e; setUiLocked(false); });
cancelBtn.onclick = () => { ws.send('cancel'); running = false; };
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
  } catch (e) {
    console.error(e);
  }
};

async function loadJigglerConfig() {
  try {
    const j = await fetch('/api/jiggler').then(r => r.json());
    jiggleEnabled.checked = j.enabled;
    jiggleInterval.value = Math.max(1, Math.round(j.interval_ms / 1000));
    jiggleDistance.value = j.distance;
    jigglePattern.value = j.pattern || 'random';
    jiggleRandomize.checked = j.randomize;
    const hasMouse = j.has_mouse;
    jiggleStatus.textContent = hasMouse ? 'Mouse HID available' : 'Mouse HID not available on this backend';
    jiggleEnabled.disabled = !hasMouse;
    jiggleSaveBtn.disabled = !hasMouse;
  } catch (e) {
    console.error(e);
    jiggleStatus.textContent = 'Failed to load jiggler config';
  }
}

jiggleSaveBtn.onclick = async () => {
  const params = new URLSearchParams();
  params.append('enabled', jiggleEnabled.checked ? '1' : '0');
  params.append('interval_ms', String(Math.max(1, parseInt(jiggleInterval.value, 10) || 1) * 1000));
  params.append('distance', String(Math.max(1, Math.min(50, parseInt(jiggleDistance.value, 10) || 2))));
  params.append('pattern', jigglePattern.value);
  params.append('randomize', jiggleRandomize.checked ? '1' : '0');
  try {
    await fetch('/api/jiggler', { method: 'POST', body: params });
    jiggleStatus.textContent = 'Saved. Toggle will take effect immediately.';
    if (ws && ws.readyState === WebSocket.OPEN) ws.send('jiggle_get');
  } catch (e) {
    jiggleStatus.textContent = 'Save failed: ' + e;
  }
};

loadJigglerConfig();
if (ws) ws.send('jiggle_get');

updateStats();
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
    String msg = "status|";
    msg += state_name(s.state);
    msg += "|" + String(s.chars_typed);
    msg += "|" + String(s.total_chars);
    msg += "|" + String(s.pending);
    msg += "|" + String(s.wpm);
    ws.textAll(msg);
}

static void ws_send(AsyncWebSocketClient* client, const String& msg) {
    if (client) client->text(msg);
}

static typing::Config mode_to_config(const String& mode) {
    if (mode == "max") return typing::preset(typing::Mode::MAX_SPEED);
    if (mode == "human") return typing::preset(typing::Mode::HUMAN);
    return typing::preset(typing::Mode::FAST_TYPIST);
}

static void handle_ws_message(AsyncWebSocketClient* client, const String& message) {
    int sep = message.indexOf('|');
    String cmd = sep < 0 ? message : message.substring(0, sep);
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "start") {
        String rest = sep < 0 ? "" : message.substring(sep + 1);
        int sep2 = rest.indexOf('|');
        String total_str = sep2 < 0 ? rest : rest.substring(0, sep2);
        String mode = sep2 < 0 ? "fast" : rest.substring(sep2 + 1);
        int total = total_str.length() > 0 ? total_str.toInt() : -1;
        if (total < 0) total = -1;
        if (g_paster) {
            g_paster->set_config(mode_to_config(mode));
            g_paster->start(total);
            ws_broadcast_status();
        }
    } else if (cmd == "chunk") {
        String text = sep < 0 ? "" : message.substring(sep + 1);
        if (g_paster) {
            g_paster->feed(std::string(text.c_str()));
            paster::Status s = g_paster->status();
            String ack = "ack|" + String(s.chars_typed) + "|" + String(s.pending);
            ws_send(client, ack);
            ws_broadcast_status();
        }
    } else if (cmd == "cancel") {
        if (g_paster) {
            g_paster->cancel();
            ws_broadcast_status();
        }
    } else if (cmd == "ping") {
        ws_send(client, "pong");
    } else if (cmd == "jiggle") {
        String arg = sep < 0 ? "" : message.substring(sep + 1);
        arg.trim();
        bool on = (arg == "on" || arg == "1" || arg == "true");
        if (g_jiggler) {
            auto js = g_jiggler->get_settings();
            js.enabled = on;
            g_jiggler->set_settings(js);
            cfg.set_jiggler_enabled(on);
            ws_send(client, String("jiggle_ack|") + (on ? "on" : "off"));
        }
    } else if (cmd == "jiggle_cfg") {
        String rest = sep < 0 ? "" : message.substring(sep + 1);
        // interval|distance|pattern|randomize
        int p1 = rest.indexOf('|');
        int p2 = p1 < 0 ? -1 : rest.indexOf('|', p1 + 1);
        int p3 = p2 < 0 ? -1 : rest.indexOf('|', p2 + 1);
        if (p1 > 0 && p2 > p1 && p3 > p2) {
            auto js = g_jiggler ? g_jiggler->get_settings() : jiggler::Settings{};
            js.interval_ms = rest.substring(0, p1).toInt();
            js.distance = static_cast<int8_t>(rest.substring(p1 + 1, p2).toInt());
            js.pattern = jiggler::pattern_from_string(std::string(rest.substring(p2 + 1, p3).c_str()));
            js.randomize_interval = (rest.substring(p3 + 1) == "1" || rest.substring(p3 + 1) == "true");
            if (js.interval_ms < 1000) js.interval_ms = 1000;
            if (js.distance < 1) js.distance = 1;
            if (js.distance > 50) js.distance = 50;
            if (g_jiggler) g_jiggler->set_settings(js);
            cfg.set_jiggler_interval_ms(js.interval_ms);
            cfg.set_jiggler_distance(js.distance);
            cfg.set_jiggler_pattern(jiggler::pattern_to_string(js.pattern));
            cfg.set_jiggler_randomize(js.randomize_interval);
            ws_send(client, "jiggle_ack|cfg");
        }
    } else if (cmd == "jiggle_get") {
        if (!g_jiggler) return;
        auto js = g_jiggler->get_settings();
        String msg = "jiggle_state|";
        msg += js.enabled ? "1" : "0";
        msg += "|" + String(js.interval_ms);
        msg += "|" + String(js.distance);
        msg += "|" + String(jiggler::pattern_to_string(js.pattern));
        msg += "|" + String(js.randomize_interval ? "1" : "0");
        msg += "|" + String(backend && backend->has_mouse() ? "1" : "0");
        ws_send(client, msg);
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

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

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

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "rebooting");
        delay(100);
        ESP.restart();
    });

    server.on("/api/jiggler", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto js = g_jiggler ? g_jiggler->get_settings() : jiggler::Settings{};
        String body = "{";
        body += "\"enabled\":" + String(js.enabled ? "true" : "false");
        body += ",\"interval_ms\":" + String(js.interval_ms);
        body += ",\"distance\":" + String(js.distance);
        body += ",\"pattern\":\"" + String(jiggler::pattern_to_string(js.pattern)) + "\"";
        body += ",\"randomize\":" + String(js.randomize_interval ? "true" : "false");
        body += ",\"has_mouse\":" + String(backend && backend->has_mouse() ? "true" : "false");
        body += "}";
        request->send(200, "application/json", body);
    });

    server.on("/api/jiggler", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!g_jiggler) {
            request->send(500, "text/plain", "jiggler not initialized");
            return;
        }
        auto js = g_jiggler->get_settings();
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
            js.pattern = jiggler::pattern_from_string(std::string(request->getParam("pattern", true)->value().c_str()));
            changed = true;
        }
        if (request->hasParam("randomize", true)) {
            String v = request->getParam("randomize", true)->value();
            js.randomize_interval = (v == "1" || v == "true");
            changed = true;
        }
        if (changed) {
            g_jiggler->set_settings(js);
            cfg.set_jiggler_enabled(js.enabled);
            cfg.set_jiggler_interval_ms(js.interval_ms);
            cfg.set_jiggler_distance(js.distance);
            cfg.set_jiggler_pattern(jiggler::pattern_to_string(js.pattern));
            cfg.set_jiggler_randomize(js.randomize_interval);
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

    server.begin();
    Serial.println("[Web] UI ready at http://" + WiFi.softAPIP().toString());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting ESP32 Paste Dongle");

    cfg.begin();
    if (cfg.get_device_name().empty()) {
        cfg.set_device_name(DEVICE_NAME);
    }
    randomSeed(esp_random());

    Serial.print("Device name: ");
    Serial.println(cfg.get_device_name().c_str());
    print_help();

    backend = hid::create_backend();
    if (!backend || !backend->begin()) {
        Serial.println("Failed to start HID backend");
        return;
    }
    backend->set_layout(cfg.get_layout());
    Serial.printf("[Layout] host layout: %s\n", cfg.get_layout().c_str());

    g_paster = new paster::Paster(backend, typing::preset(typing::Mode::FAST_TYPIST));

    jiggler::Settings js;
    js.enabled = cfg.get_jiggler_enabled();
    js.interval_ms = cfg.get_jiggler_interval_ms();
    js.distance = cfg.get_jiggler_distance();
    js.pattern = jiggler::pattern_from_string(cfg.get_jiggler_pattern());
    js.randomize_interval = cfg.get_jiggler_randomize();
    g_jiggler = new jiggler::Jiggler(backend);
    g_jiggler->set_settings(js);
    Serial.printf("[Jiggler] enabled=%d interval=%ums distance=%d pattern=%s random=%d\n",
                  js.enabled, js.interval_ms, js.distance,
                  jiggler::pattern_to_string(js.pattern), js.randomize_interval);

    Serial.println("Backend started. Waiting for host connection...");
    start_web_ui();
}

void loop() {
    if (!backend) return;

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        handle_serial_command(line);
    }

    bool connected = backend->is_connected();
    if (connected && !was_connected) {
        Serial.println("Host connected.");
    }
    was_connected = connected;

    if (g_jiggler) {
        g_jiggler->tick();
    }

    if (g_paster) {
        g_paster->tick();

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

    delay(5);
}
