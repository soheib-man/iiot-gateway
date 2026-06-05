#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

// ─── ADC pins ─────────────────────────────────────────────
#define NTC10K_PIN   1
#define NTC20K_PIN   2
#define QFM_U1_PIN   4   // humidity
#define QFM_U2_PIN   3   // temperature

// ─── NTC defaults ─────────────────────────────────────────
#define DEF_SERIES_10K  10000.0
#define DEF_NOMINAL_10K 10000.0
#define DEF_SERIES_20K  22000.0
#define DEF_NOMINAL_20K 20000.0
#define DEF_BCOEF       3950.0
#define DEF_NOMINAL_T   25.0
#define ADC_MAX         4095.0

// ─── QFM defaults ─────────────────────────────────────────
#define DEF_R1          22000.0
#define DEF_R2          10000.0
#define DEF_HUM_MAX     100.0
#define DEF_TEMP_MAX    50.0
#define DEF_TEMP_OFFSET -3.5

// ─── AP/STA defaults ──────────────────────────────────────
#define DEF_AP_SSID    "IIoT-Sensors"
#define DEF_AP_PASS    "12345678"
#define DEF_HOSTNAME   "sensor"
#define DEF_STA_SSID   ""
#define DEF_STA_PASS   ""

// ─── Globals ──────────────────────────────────────────────
AsyncWebServer server(80);
Preferences    prefs;

// Config
String  apSSID, apPass, hostname;
String  staSSID, staPass;
float   series10k, nominal10k;
float   series20k, nominal20k;
float   bcoef, nominalT;
float   qfmR1, qfmR2, qfmHumMax, qfmTempMax, qfmTempOffset;
uint32_t readInterval;

// Readings
float lastNTC10K  = -999;
float lastNTC20K  = -999;
float lastQFMHum  = -999;
float lastQFMTemp = -999;
unsigned long lastReadTime = 0;

// ─── Config load/save ─────────────────────────────────────
void loadConfig() {
  prefs.begin("cfg", true);
  apSSID        = prefs.getString("apSSID",    DEF_AP_SSID);
  apPass        = prefs.getString("apPass",    DEF_AP_PASS);
  hostname      = prefs.getString("hostname",  DEF_HOSTNAME);
  staSSID       = prefs.getString("staSSID",   DEF_STA_SSID);
  staPass       = prefs.getString("staPass",   DEF_STA_PASS);
  series10k     = prefs.getFloat ("s10k",      DEF_SERIES_10K);
  nominal10k    = prefs.getFloat ("n10k",      DEF_NOMINAL_10K);
  series20k     = prefs.getFloat ("s20k",      DEF_SERIES_20K);
  nominal20k    = prefs.getFloat ("n20k",      DEF_NOMINAL_20K);
  bcoef         = prefs.getFloat ("bcoef",     DEF_BCOEF);
  nominalT      = prefs.getFloat ("nomT",      DEF_NOMINAL_T);
  qfmR1         = prefs.getFloat ("qfmR1",     DEF_R1);
  qfmR2         = prefs.getFloat ("qfmR2",     DEF_R2);
  qfmHumMax     = prefs.getFloat ("qfmHMax",   DEF_HUM_MAX);
  qfmTempMax    = prefs.getFloat ("qfmTMax",   DEF_TEMP_MAX);
  qfmTempOffset = prefs.getFloat ("qfmTOff",   DEF_TEMP_OFFSET);
  readInterval  = prefs.getUInt  ("interval",  2000);
  prefs.end();
}

void saveConfig() {
  prefs.begin("cfg", false);
  prefs.putString("apSSID",   apSSID);
  prefs.putString("apPass",   apPass);
  prefs.putString("hostname", hostname);
  prefs.putString("staSSID",  staSSID);
  prefs.putString("staPass",  staPass);
  prefs.putFloat ("s10k",     series10k);
  prefs.putFloat ("n10k",     nominal10k);
  prefs.putFloat ("s20k",     series20k);
  prefs.putFloat ("n20k",     nominal20k);
  prefs.putFloat ("bcoef",    bcoef);
  prefs.putFloat ("nomT",     nominalT);
  prefs.putFloat ("qfmR1",    qfmR1);
  prefs.putFloat ("qfmR2",    qfmR2);
  prefs.putFloat ("qfmHMax",  qfmHumMax);
  prefs.putFloat ("qfmTMax",  qfmTempMax);
  prefs.putFloat ("qfmTOff",  qfmTempOffset);
  prefs.putUInt  ("interval", readInterval);
  prefs.end();
}

// ─── Sensor readers ───────────────────────────────────────
float readNTC(int pin, float seriesR, float nomR) {
  int raw = analogRead(pin);
  if (raw <= 0) return -999.0;
  float v = (raw / ADC_MAX) * 3.3;
  if (v <= 0) return -999.0;
  float r = seriesR * (3.3 / v - 1.0);
  if (r <= 0) return -999.0;
  float s = log(r / nomR);
  s /= bcoef;
  s += 1.0 / (nominalT + 273.15);
  return (1.0 / s) - 273.15;
}

float readQFMHum(int pin) {
  int raw = analogRead(pin);
  float vADC = (raw / ADC_MAX) * 3.3;
  if (vADC < 0.1) return -999.0;
  float vSensor = vADC * ((qfmR1 + qfmR2) / qfmR2);
  if (vSensor > 10.0) vSensor = 10.0;
  return (vSensor / 10.0) * qfmHumMax;
}

float readQFMTemp(int pin) {
  int raw = analogRead(pin);
  float vADC = (raw / ADC_MAX) * 3.3;
  if (vADC < 0.1) return -999.0;
  float vSensor = vADC * ((qfmR1 + qfmR2) / qfmR2);
  if (vSensor > 10.0) vSensor = 10.0;
  return ((vSensor / 10.0) * qfmTempMax) + qfmTempOffset;
}

// ─── WiFi init ────────────────────────────────────────────
void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), apPass.c_str());
  Serial.printf("AP: %s | IP: %s\n",
    apSSID.c_str(),
    WiFi.softAPIP().toString().c_str());

  if (staSSID.length() > 0) {
    WiFi.begin(staSSID.c_str(), staPass.c_str());
    Serial.print("Connecting to router");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      Serial.print(".");
      tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nSTA IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("\nSTA failed — AP only mode");
    }
  }
}

// ─── Dashboard HTML ───────────────────────────────────────
const char DASHBOARD[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IIoT Sensor Dashboard</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Syne:wght@400;700;800&display=swap');
  :root {
    --bg:     #0a0e14;
    --panel:  #111720;
    --border: #1e2d3d;
    --accent: #00d4ff;
    --green:  #00ff9d;
    --amber:  #ffb347;
    --red:    #ff4757;
    --text:   #c8d6e5;
    --muted:  #4a6075;
    --mono:   'Share Tech Mono', monospace;
    --sans:   'Syne', sans-serif;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--text); font-family: var(--sans); min-height: 100vh; padding: 24px; }
  header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 32px; padding-bottom: 16px; border-bottom: 1px solid var(--border); }
  header h1 { font-size: 22px; font-weight: 800; letter-spacing: -0.5px; color: #fff; }
  header h1 span { color: var(--accent); }
  .status { display: flex; align-items: center; gap: 8px; font-family: var(--mono); font-size: 12px; }
  .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--green); box-shadow: 0 0 8px var(--green); animation: pulse 2s ease-in-out infinite; }
  .dot.offline { background: var(--red); box-shadow: 0 0 8px var(--red); animation: none; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.4} }
  .sta-status { font-family: var(--mono); font-size: 11px; color: var(--muted); margin-bottom: 24px; }
  .sta-status span { color: var(--green); }
  .sta-status span.disconnected { color: var(--red); }
  .readings { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 32px; }
  .card { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 20px; position: relative; overflow: hidden; transition: border-color 0.2s; }
  .card:hover { border-color: var(--accent); }
  .card::before { content: ''; position: absolute; top: 0; left: 0; right: 0; height: 2px; background: var(--accent-color, var(--accent)); }
  .card.ntc10  { --accent-color: var(--accent); }
  .card.ntc20  { --accent-color: var(--green); }
  .card.qfmhum { --accent-color: var(--amber); }
  .card.qfmtmp { --accent-color: #b47eff; }
  .card-label { font-family: var(--mono); font-size: 11px; color: var(--muted); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 10px; }
  .card-value { font-family: var(--mono); font-size: 36px; font-weight: 400; color: #fff; line-height: 1; }
  .card-unit  { font-family: var(--mono); font-size: 14px; color: var(--muted); margin-left: 4px; }
  .card-pin   { font-family: var(--mono); font-size: 10px; color: var(--muted); margin-top: 8px; }
  .config-header { font-size: 18px; font-weight: 700; color: #fff; margin-bottom: 20px; display: flex; align-items: center; gap: 10px; }
  .config-header::after { content: ''; flex: 1; height: 1px; background: var(--border); }
  .config-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; margin-bottom: 24px; }
  .config-card { background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 20px; }
  .config-card h3 { font-size: 13px; font-weight: 700; color: var(--accent); margin-bottom: 16px; font-family: var(--mono); text-transform: uppercase; letter-spacing: 1px; }
  .field { margin-bottom: 12px; }
  .field label { display: block; font-size: 11px; color: var(--muted); font-family: var(--mono); margin-bottom: 5px; text-transform: uppercase; letter-spacing: 0.5px; }
  .field input { width: 100%; background: var(--bg); border: 1px solid var(--border); border-radius: 6px; padding: 8px 10px; color: var(--text); font-family: var(--mono); font-size: 13px; outline: none; transition: border-color 0.2s; }
  .field input:focus { border-color: var(--accent); }
  .interval-row { display: flex; align-items: center; gap: 12px; margin-bottom: 24px; }
  .interval-row label { font-family: var(--mono); font-size: 11px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.5px; white-space: nowrap; }
  .interval-row input { width: 100px; background: var(--bg); border: 1px solid var(--border); border-radius: 6px; padding: 8px 10px; color: var(--text); font-family: var(--mono); font-size: 13px; outline: none; }
  .save-btn { background: var(--accent); color: #000; border: none; border-radius: 8px; padding: 12px 32px; font-family: var(--sans); font-size: 14px; font-weight: 700; cursor: pointer; transition: opacity 0.2s, transform 0.1s; letter-spacing: 0.5px; }
  .save-btn:hover  { opacity: 0.85; }
  .save-btn:active { transform: scale(0.98); }
  .toast { position: fixed; bottom: 24px; right: 24px; background: var(--green); color: #000; padding: 12px 20px; border-radius: 8px; font-family: var(--mono); font-size: 13px; font-weight: 700; opacity: 0; transform: translateY(10px); transition: all 0.3s; pointer-events: none; }
  .toast.show { opacity: 1; transform: translateY(0); }
</style>
</head>
<body>
<header>
  <h1>IIoT <span>Sensor</span> Dashboard</h1>
  <div class="status">
    <div class="dot" id="dot"></div>
    <span id="status-label">LIVE</span>
  </div>
</header>

<div class="sta-status">
  Router: <span id="sta-status">Checking...</span> |
  STA IP: <span id="sta-ip">--</span>
</div>

<div class="readings">
  <div class="card ntc10">
    <div class="card-label">NTC 10K</div>
    <div class="card-value" id="v-ntc10">--<span class="card-unit">°C</span></div>
    <div class="card-pin">GPIO1</div>
  </div>
  <div class="card ntc20">
    <div class="card-label">NTC 20K</div>
    <div class="card-value" id="v-ntc20">--<span class="card-unit">°C</span></div>
    <div class="card-pin">GPIO2</div>
  </div>
  <div class="card qfmhum">
    <div class="card-label">QFM3160 Humidity</div>
    <div class="card-value" id="v-qfmhum">--<span class="card-unit">%RH</span></div>
    <div class="card-pin">GPIO4 — U1</div>
  </div>
  <div class="card qfmtmp">
    <div class="card-label">QFM3160 Temp</div>
    <div class="card-value" id="v-qfmtmp">--<span class="card-unit">°C</span></div>
    <div class="card-pin">GPIO3 — U2</div>
  </div>
</div>

<div class="config-header">Configuration</div>

<div class="interval-row">
  <label>Read interval (ms)</label>
  <input type="number" id="cfg-interval">
</div>

<div class="config-grid">

  <div class="config-card">
    <h3>Access Point</h3>
    <div class="field"><label>AP SSID</label><input type="text" id="cfg-apSSID"></div>
    <div class="field"><label>AP Password</label><input type="password" id="cfg-apPass"></div>
    <div class="field"><label>Hostname (mDNS)</label><input type="text" id="cfg-hostname"></div>
  </div>

  <div class="config-card">
    <h3>WiFi Router (STA)</h3>
    <div class="field"><label>Router SSID</label><input type="text" id="cfg-staSSID"></div>
    <div class="field"><label>Router Password</label><input type="password" id="cfg-staPass"></div>
  </div>

  <div class="config-card">
    <h3>NTC 10K</h3>
    <div class="field"><label>Series resistor (Ω)</label><input type="number" id="cfg-s10k"></div>
    <div class="field"><label>Nominal resistance (Ω)</label><input type="number" id="cfg-n10k"></div>
  </div>

  <div class="config-card">
    <h3>NTC 20K</h3>
    <div class="field"><label>Series resistor (Ω)</label><input type="number" id="cfg-s20k"></div>
    <div class="field"><label>Nominal resistance (Ω)</label><input type="number" id="cfg-n20k"></div>
  </div>

  <div class="config-card">
    <h3>NTC Shared</h3>
    <div class="field"><label>B coefficient</label><input type="number" id="cfg-bcoef"></div>
    <div class="field"><label>Nominal temp (°C)</label><input type="number" id="cfg-nomt"></div>
  </div>

  <div class="config-card">
    <h3>QFM3160</h3>
    <div class="field"><label>R1 top (Ω)</label><input type="number" id="cfg-qfmR1"></div>
    <div class="field"><label>R2 bottom (Ω)</label><input type="number" id="cfg-qfmR2"></div>
    <div class="field"><label>Humidity max (%RH)</label><input type="number" id="cfg-qfmHMax"></div>
    <div class="field"><label>Temp max (°C)</label><input type="number" id="cfg-qfmTMax"></div>
    <div class="field"><label>Temp offset (°C)</label><input type="number" step="0.1" id="cfg-qfmTOff"></div>
  </div>

</div>

<button class="save-btn" onclick="save()">Save & Apply</button>
<div class="toast" id="toast">Saved successfully</div>

<script>
async function fetchData() {
  try {
    const r = await fetch('/api/data');
    const d = await r.json();
    set('v-ntc10',  d.ntc10k,  '°C');
    set('v-ntc20',  d.ntc20k,  '°C');
    set('v-qfmhum', d.qfmHum,  '%RH');
    set('v-qfmtmp', d.qfmTemp, '°C');
    const staEl = document.getElementById('sta-status');
    const ipEl  = document.getElementById('sta-ip');
    if (d.staConnected) {
      staEl.textContent = 'Connected';
      staEl.className   = '';
      ipEl.textContent  = d.staIP;
    } else {
      staEl.textContent = 'Disconnected';
      staEl.className   = 'disconnected';
      ipEl.textContent  = '--';
    }
    document.getElementById('dot').className = 'dot';
    document.getElementById('status-label').textContent = 'LIVE';
  } catch(e) {
    document.getElementById('dot').className = 'dot offline';
    document.getElementById('status-label').textContent = 'OFFLINE';
  }
}

function set(id, val, unit) {
  const el = document.getElementById(id);
  if (val === null || val === undefined || val <= -900) {
    el.innerHTML = '<span style="font-size:13px;color:#ff4757">ERROR</span>';
  } else {
    el.innerHTML = val.toFixed(1) + '<span class="card-unit">' + unit + '</span>';
  }
}

async function loadConfig() {
  const r = await fetch('/api/config');
  const d = await r.json();
  document.getElementById('cfg-apSSID').value   = d.apSSID;
  document.getElementById('cfg-apPass').value   = d.apPass;
  document.getElementById('cfg-hostname').value = d.hostname;
  document.getElementById('cfg-staSSID').value  = d.staSSID;
  document.getElementById('cfg-staPass').value  = d.staPass;
  document.getElementById('cfg-s10k').value     = d.series10k;
  document.getElementById('cfg-n10k').value     = d.nominal10k;
  document.getElementById('cfg-s20k').value     = d.series20k;
  document.getElementById('cfg-n20k').value     = d.nominal20k;
  document.getElementById('cfg-bcoef').value    = d.bcoef;
  document.getElementById('cfg-nomt').value     = d.nominalT;
  document.getElementById('cfg-qfmR1').value    = d.qfmR1;
  document.getElementById('cfg-qfmR2').value    = d.qfmR2;
  document.getElementById('cfg-qfmHMax').value  = d.qfmHumMax;
  document.getElementById('cfg-qfmTMax').value  = d.qfmTempMax;
  document.getElementById('cfg-qfmTOff').value  = d.qfmTempOffset;
  document.getElementById('cfg-interval').value = d.interval;
}

async function save() {
  const body = {
    apSSID:       document.getElementById('cfg-apSSID').value,
    apPass:       document.getElementById('cfg-apPass').value,
    hostname:     document.getElementById('cfg-hostname').value,
    staSSID:      document.getElementById('cfg-staSSID').value,
    staPass:      document.getElementById('cfg-staPass').value,
    series10k:    parseFloat(document.getElementById('cfg-s10k').value),
    nominal10k:   parseFloat(document.getElementById('cfg-n10k').value),
    series20k:    parseFloat(document.getElementById('cfg-s20k').value),
    nominal20k:   parseFloat(document.getElementById('cfg-n20k').value),
    bcoef:        parseFloat(document.getElementById('cfg-bcoef').value),
    nominalT:     parseFloat(document.getElementById('cfg-nomt').value),
    qfmR1:        parseFloat(document.getElementById('cfg-qfmR1').value),
    qfmR2:        parseFloat(document.getElementById('cfg-qfmR2').value),
    qfmHumMax:    parseFloat(document.getElementById('cfg-qfmHMax').value),
    qfmTempMax:   parseFloat(document.getElementById('cfg-qfmTMax').value),
    qfmTempOffset:parseFloat(document.getElementById('cfg-qfmTOff').value),
    interval:     parseInt(document.getElementById('cfg-interval').value)
  };
  await fetch('/api/config', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(body)
  });
  const t = document.getElementById('toast');
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2500);
}

loadConfig();
fetchData();
setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawhtml";

// ─── Web routes ───────────────────────────────────────────
void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", DASHBOARD);
  });

  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["ntc10k"]      = (lastNTC10K  > -900) ? lastNTC10K  : (float)NULL;
    doc["ntc20k"]      = (lastNTC20K  > -900) ? lastNTC20K  : (float)NULL;
    doc["qfmHum"]      = (lastQFMHum  > -900) ? lastQFMHum  : (float)NULL;
    doc["qfmTemp"]     = (lastQFMTemp > -900) ? lastQFMTemp : (float)NULL;
    doc["staConnected"]= (WiFi.status() == WL_CONNECTED);
    doc["staIP"]       = WiFi.localIP().toString();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["apSSID"]       = apSSID;
    doc["apPass"]       = apPass;
    doc["hostname"]     = hostname;
    doc["staSSID"]      = staSSID;
    doc["staPass"]      = staPass;
    doc["series10k"]    = series10k;
    doc["nominal10k"]   = nominal10k;
    doc["series20k"]    = series20k;
    doc["nominal20k"]   = nominal20k;
    doc["bcoef"]        = bcoef;
    doc["nominalT"]     = nominalT;
    doc["qfmR1"]        = qfmR1;
    doc["qfmR2"]        = qfmR2;
    doc["qfmHumMax"]    = qfmHumMax;
    doc["qfmTempMax"]   = qfmTempMax;
    doc["qfmTempOffset"]= qfmTempOffset;
    doc["interval"]     = readInterval;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest *req){},
    NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      JsonDocument doc;
      if (deserializeJson(doc, data, len)) {
        req->send(400, "text/plain", "Bad JSON");
        return;
      }
      apSSID        = doc["apSSID"]       .as<String>();
      apPass        = doc["apPass"]       .as<String>();
      hostname      = doc["hostname"]     .as<String>();
      staSSID       = doc["staSSID"]      .as<String>();
      staPass       = doc["staPass"]      .as<String>();
      series10k     = doc["series10k"]    .as<float>();
      nominal10k    = doc["nominal10k"]   .as<float>();
      series20k     = doc["series20k"]    .as<float>();
      nominal20k    = doc["nominal20k"]   .as<float>();
      bcoef         = doc["bcoef"]        .as<float>();
      nominalT      = doc["nominalT"]     .as<float>();
      qfmR1         = doc["qfmR1"]        .as<float>();
      qfmR2         = doc["qfmR2"]        .as<float>();
      qfmHumMax     = doc["qfmHumMax"]    .as<float>();
      qfmTempMax    = doc["qfmTempMax"]   .as<float>();
      qfmTempOffset = doc["qfmTempOffset"].as<float>();
      readInterval  = doc["interval"]     .as<uint32_t>();
      saveConfig();
      WiFi.softAP(apSSID.c_str(), apPass.c_str());
      MDNS.begin(hostname.c_str());
      if (staSSID.length() > 0) {
        WiFi.begin(staSSID.c_str(), staPass.c_str());
      }
      req->send(200, "text/plain", "OK");
    }
  );
}

// ─── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  loadConfig();
  initWiFi();
  if (MDNS.begin(hostname.c_str())) {
    Serial.printf("mDNS: http://%s.local\n", hostname.c_str());
  }
  setupRoutes();
  server.begin();
  Serial.println("Server ready");
}

// ─── Loop ─────────────────────────────────────────────────
void loop() {
  if (millis() - lastReadTime >= readInterval) {
    lastReadTime = millis();
    lastNTC10K  = readNTC(NTC10K_PIN,  series10k,  nominal10k);
    lastNTC20K  = readNTC(NTC20K_PIN,  series20k,  nominal20k);
    lastQFMHum  = readQFMHum(QFM_U1_PIN);
    lastQFMTemp = readQFMTemp(QFM_U2_PIN);
    Serial.printf("NTC10K: %.2f | NTC20K: %.2f | Hum: %.1f%%RH | Temp: %.2f°C\n",
      lastNTC10K, lastNTC20K, lastQFMHum, lastQFMTemp);
  }
}