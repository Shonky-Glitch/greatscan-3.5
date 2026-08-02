#include "web_ui.h"

#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <cstdio>

#include "project_config.h"

namespace {
WebServer server(80);
HardwareSerial greatScanSerial(1);
constexpr unsigned long kTelemetryStaleMs = 1800;
constexpr unsigned long kSerialTelemetryStaleMs = 1200;
constexpr unsigned long kCanFieldStaleMs = 3000;
constexpr uint16_t kStatusTimeoutMs = 220;
constexpr uint16_t kRawTimeoutMs = 180;
constexpr uint16_t kCommandTimeoutMs = 700;
constexpr unsigned long kStaReconnectIntervalMs = 5000;
constexpr unsigned long kStatusFailBackoffBaseMs = 700;
constexpr unsigned long kRawFailBackoffBaseMs = 900;
constexpr unsigned long kPollFailBackoffCapMs = 5000;
constexpr float kKpaToPsi = 0.14503774f;

unsigned long scaledBackoffMs(const unsigned long baseMs, const uint8_t failureCount) {
  if (failureCount == 0) {
    return 0;
  }

  const uint8_t shift = failureCount > 3 ? 3 : static_cast<uint8_t>(failureCount - 1);
  unsigned long scaled = baseMs << shift;
  if (scaled > kPollFailBackoffCapMs) {
    scaled = kPollFailBackoffCapMs;
  }
  return scaled;
}

void appendFloatOrNull(String& json, const char* key, const bool valid, const float value, const uint8_t decimals) {
  json += "\"";
  json += key;
  json += "\":";
  if (valid) {
    json += String(value, static_cast<unsigned int>(decimals));
  } else {
    json += "null";
  }
}

void appendCanFieldOrUnknown(String& json, const char* key, const String& value, const unsigned long lastUpdateMs) {
  const bool fresh = lastUpdateMs != 0 && (millis() - lastUpdateMs) <= kCanFieldStaleMs;
  json += "\"";
  json += key;
  json += "\":\"";
  json += fresh ? value : String("UNKNOWN");
  json += "\"";
}

bool extractJsonString(const String& payload, const char* key, String& out) {
  const String token = String("\"") + key + "\":\"";
  const int start = payload.indexOf(token);
  if (start < 0) return false;
  const int valueStart = start + token.length();
  const int valueEnd = payload.indexOf('"', valueStart);
  if (valueEnd < 0) return false;
  out = payload.substring(valueStart, valueEnd);
  return true;
}

bool extractJsonInt(const String& payload, const char* key, int& out) {
  const String token = String("\"") + key + "\":";
  const int start = payload.indexOf(token);
  if (start < 0) return false;
  int valueStart = start + token.length();
  int valueEnd = valueStart;
  while (valueEnd < payload.length() && isDigit(payload[valueEnd])) {
    valueEnd++;
  }
  if (valueEnd == valueStart) return false;
  out = payload.substring(valueStart, valueEnd).toInt();
  return true;
}

bool extractJsonFloat(const String& payload, const char* key, float& out) {
  const String token = String("\"") + key + "\":";
  const int start = payload.indexOf(token);
  if (start < 0) return false;

  int valueStart = start + token.length();
  int valueEnd = valueStart;
  while (valueEnd < payload.length()) {
    const char c = payload[valueEnd];
    if (!(isDigit(c) || c == '-' || c == '+' || c == '.')) {
      break;
    }
    valueEnd++;
  }
  if (valueEnd == valueStart) return false;

  out = payload.substring(valueStart, valueEnd).toFloat();
  return true;
}

String trimForUi(const String& text, const size_t maxLen) {
  if (text.length() <= maxLen) {
    return text;
  }
  return text.substring(0, static_cast<int>(maxLen));
}

const char kIndexHtml[] = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>GreatScan 3.5</title>
  <style>
    :root { color-scheme: light; }
    body {
      margin: 0;
      font-family: "Trebuchet MS", "Segoe UI", sans-serif;
      background: radial-gradient(circle at 10% 10%, #ffe29a, #ffc26e 40%, #ff9f6d 70%, #f8f2e9 100%);
      min-height: 100vh;
      display: grid;
      place-items: center;
    }
    .card {
      width: min(92vw, 520px);
      background: rgba(255, 255, 255, 0.86);
      border: 1px solid rgba(0, 0, 0, 0.08);
      border-radius: 18px;
      padding: 20px;
      box-shadow: 0 20px 40px rgba(0, 0, 0, 0.12);
      backdrop-filter: blur(5px);
    }
    h1 { margin: 0 0 12px; font-size: 1.3rem; }
    .head-badge {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      margin-left: 8px;
      padding: 3px 8px;
      border-radius: 8px;
      font-size: 0.8rem;
      font-weight: 800;
      border: 1px solid rgba(0, 0, 0, 0.18);
      color: #fff;
      background: #6b7280;
      vertical-align: middle;
      min-width: 72px;
    }
    .head-badge.ign-off { background: #b91c1c; }
    .head-badge.ign-run { background: #15803d; }
    .head-badge.ign-on  { background: #1d4ed8; }
    .row {
      display: flex;
      justify-content: space-between;
      margin: 10px 0;
      font-size: 1.05rem;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
      margin-top: 10px;
    }
    button {
      border: 0;
      border-radius: 10px;
      padding: 10px;
      font-weight: 700;
      background: #1f2937;
      color: #fff;
    }
    .value { font-weight: 700; }
  </style>
</head>
<body>
  <section class="card">
    <h1>GreatScan 3.5 Live Gauges <span id="oem">[AUTO]</span><span id="ign" class="head-badge ign-off">IGN OFF</span></h1>
    <div class="row"><span>RPM</span><span class="value" id="rpm">-</span></div>
    <div class="row"><span>Coolant</span><span class="value" id="coolant">-</span></div>
    <hr />
    <div class="row"><span>GreatScan Link</span><span class="value" id="online">-</span></div>
    <div class="row"><span>Diag Bus</span><span class="value" id="diag">-</span></div>
    <div class="row"><span>CAN1</span><span class="value" id="can1">-</span></div>
    <div class="row"><span>CAN2</span><span class="value" id="can2">-</span></div>
    <div class="row"><span>VIN</span><span class="value" id="vin">-</span></div>
    <div class="row"><span>DTC Count</span><span class="value" id="dtc">-</span></div>
    <div class="row"><span>DTC Stored/Pending/Permanent</span><span class="value" id="dtc3">-</span></div>
    <div class="row"><span>Latest DTCs</span><span class="value" id="dtclast">-</span></div>
    <div class="row"><span>UDS Summary</span><span class="value" id="udssummary">-</span></div>
    <div class="row"><span>Ford Module Mask</span><span class="value" id="fordmask">-</span></div>
    <div class="row"><span>Toyota Module Mask</span><span class="value" id="toyotamask">-</span></div>
    <div class="row"><span>Speed</span><span class="value" id="speed">-</span></div>
    <div class="row"><span>Boost</span><span class="value" id="boost">-</span></div>
    <div class="row"><span>Fuel</span><span class="value" id="fuel">-</span></div>
    <div class="row"><span>Throttle</span><span class="value" id="throttle">-</span></div>
    <div class="row"><span>DPF</span><span class="value" id="dpf">-</span></div>
    <div class="row"><span>Raw Seq</span><span class="value" id="rawseq">-</span></div>
    <div class="row"><span>Raw Line</span><span class="value" id="rawline">-</span></div>
    <div class="row"><span>Last Command</span><span class="value" id="cmdresult">-</span></div>
    <div class="grid">
      <button onclick="sendAuto('read')">Read DTC (Auto)</button>
      <button onclick="sendAuto('clear')">Clear DTC (Auto)</button>
      <button onclick="sendAuto('scan')">Module Scan (Auto)</button>
      <button onclick="sendDiag('x')">Read DTC Ford</button>
      <button onclick="sendDiag('r')">Read DTC Toyota</button>
      <button onclick="sendDiag('g')">DTC Summary</button>
    </div>
  </section>
  <script>
    async function sendDiag(cmd) {
      await fetch('/api/diag-cmd?c=' + encodeURIComponent(cmd));
      await poll();
    }
    async function sendAuto(action) {
      await fetch('/api/diag-auto?action=' + encodeURIComponent(action));
      await poll();
    }
    async function poll() {
      const gaugesResp = await fetch('/api/gauges');
      const gauges = await gaugesResp.json();
      document.getElementById('rpm').textContent = Math.round(gauges.rpm) + ' rpm';
      document.getElementById('coolant').textContent = gauges.coolant.toFixed(1) + ' C';

      const gsResp = await fetch('/api/greatscan');
      const gs = await gsResp.json();
      document.getElementById('online').textContent = gs.online ? 'online' : 'offline';
      document.getElementById('diag').textContent = gs.diag;
      document.getElementById('can1').textContent = gs.can1;
      document.getElementById('can2').textContent = gs.can2;
      document.getElementById('oem').textContent = '[' + gs.oem + ']';
      document.getElementById('vin').textContent = gs.vin;
      document.getElementById('dtc').textContent = gs.dtc;
      document.getElementById('dtc3').textContent = gs.dtcStored + ' / ' + gs.dtcPending + ' / ' + gs.dtcPermanent;
      document.getElementById('dtclast').textContent = gs.lastStored + ' | ' + gs.lastPending + ' | ' + gs.lastPermanent;
      document.getElementById('udssummary').textContent = gs.udsSummary;
      document.getElementById('fordmask').textContent = gs.fordModuleMask;
      document.getElementById('toyotamask').textContent = gs.toyotaModuleMask;
      document.getElementById('speed').textContent = gs.speed === null ? '-' : Math.round(gs.speed) + ' km/h';
      document.getElementById('boost').textContent = gs.boostPsi === null ? '-' : gs.boostPsi.toFixed(1) + ' psi';
      document.getElementById('fuel').textContent = gs.fuelLiters === null ? '-' : gs.fuelLiters.toFixed(1) + ' L left';
      document.getElementById('throttle').textContent = gs.throttle === null ? '-' : gs.throttle.toFixed(1) + ' %';
      document.getElementById('dpf').textContent = gs.dpf === null ? '-' : gs.dpf.toFixed(1) + ' %';
      document.getElementById('rawseq').textContent = gs.rawSeq;
      document.getElementById('rawline').textContent = gs.rawLine;
      document.getElementById('cmdresult').textContent = gs.lastCommand;

      const ign = document.getElementById('ign');
      let ignText = 'IGN OFF';
      let ignClass = 'head-badge ign-off';

      if (gs.online) {
        if (gauges.rpm >= 450) {
          ignText = 'IGN RUN';
          ignClass = 'head-badge ign-run';
        } else if (gs.voltage !== null && gs.voltage >= 12.2) {
          ignText = 'IGN ON';
          ignClass = 'head-badge ign-on';
        }
      }

      ign.textContent = ignText;
      ign.className = ignClass;
    }
    poll();
    setInterval(poll, 350);
  </script>
</body>
</html>
)HTML";
}  // namespace

void WebUi::begin(
  float* rpmValue,
  float* coolantValue,
  float* speedValue,
  float* boostValue,
  float* fuelValue,
  float* throttleValue,
  float* dpfValue,
  float* oilPressureValue,
  float* afrValue,
  float* oilTempValue,
  float* voltageValue,
  float* atfValue,
  float* eg3Value,
  float* o2s1Value,
  float* engineSpeedDidValue) {
  rpm_ = rpmValue;
  coolantC_ = coolantValue;
  speedKph_ = speedValue;
  boostKpa_ = boostValue;
  fuelPct_ = fuelValue;
  throttlePct_ = throttleValue;
  dpfPct_ = dpfValue;
  oilPressureKpa_ = oilPressureValue;
  afr_ = afrValue;
  oilTempC_ = oilTempValue;
  voltage_ = voltageValue;
  atfC_ = atfValue;
  eg3C_ = eg3Value;
  o2s1V_ = o2s1Value;
  engineSpeedDidRpm_ = engineSpeedDidValue;

  stationEnabled_ = ProjectConfig::kStaSsid[0] != '\0';
  const IPAddress apIp(
      ProjectConfig::kApIpA,
      ProjectConfig::kApIpB,
      ProjectConfig::kApIpC,
      ProjectConfig::kApIpD);
  const IPAddress apGateway(
      ProjectConfig::kApIpA,
      ProjectConfig::kApIpB,
      ProjectConfig::kApIpC,
      ProjectConfig::kApIpD);
  const IPAddress apSubnet(
      ProjectConfig::kApSubnetA,
      ProjectConfig::kApSubnetB,
      ProjectConfig::kApSubnetC,
      ProjectConfig::kApSubnetD);

  if (stationEnabled_) {
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAPConfig(apIp, apGateway, apSubnet)) {
      Serial.println("[WEB] AP config failed; using default AP subnet");
    }
    WiFi.softAP(ProjectConfig::kApSsid, ProjectConfig::kApPassword);
    WiFi.begin(ProjectConfig::kStaSsid, ProjectConfig::kStaPassword);
    lastStationReconnectMs_ = millis();
    if (WiFi.waitForConnectResult(12000) != WL_CONNECTED) {
      Serial.println("[WEB] STA connect failed; continuing AP-only service");
      stationConnectedLogged_ = false;
    } else {
      stationConnectedLogged_ = true;
    }
  } else {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAPConfig(apIp, apGateway, apSubnet)) {
      Serial.println("[WEB] AP config failed; using default AP subnet");
    }
    WiFi.softAP(ProjectConfig::kApSsid, ProjectConfig::kApPassword);
  }

  if (ProjectConfig::kGreatScanSerialBridgeEnabled) {
    greatScanSerial.begin(
        ProjectConfig::kGreatScanSerialBridgeBaud,
        SERIAL_8N1,
        ProjectConfig::kGreatScanSerialBridgeRxPin,
        ProjectConfig::kGreatScanSerialBridgeTxPin);
    serialLineBuffer_.reserve(320);
    Serial.printf(
        "[GS] Serial bridge enabled baud=%lu RX=%d TX=%d\n",
        static_cast<unsigned long>(ProjectConfig::kGreatScanSerialBridgeBaud),
        ProjectConfig::kGreatScanSerialBridgeRxPin,
        ProjectConfig::kGreatScanSerialBridgeTxPin);
  }

  server.on("/", []() { server.send(200, "text/html", kIndexHtml); });
  server.on("/api/gauges", [this]() {
    const float rpm = rpm_ ? *rpm_ : 0.0f;
    const float coolant = coolantC_ ? *coolantC_ : 0.0f;

    String json = "{\"rpm\":";
    json += String(rpm, 2);
    json += ",\"coolant\":";
    json += String(coolant, 2);
    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/api/greatscan", [this]() {
    String json = "{";
    json += "\"online\":";
    json += remoteOnline_ ? "true" : "false";
    json += ",\"diag\":\"" + remoteDiag_ + "\"";
    json += ",";
    appendCanFieldOrUnknown(json, "can1", remoteCan1_, lastCan1UpdateMs_);
    json += ",";
    appendCanFieldOrUnknown(json, "can2", remoteCan2_, lastCan2UpdateMs_);
    json += ",\"vin\":\"" + remoteVin_ + "\"";
    json += ",\"oem\":\"" + detectedVehicle_ + "\"";
    json += ",\"dtc\":" + String(remoteDtcCount_);
    json += ",\"dtcStored\":" + String(remoteDtcStored_);
    json += ",\"dtcPending\":" + String(remoteDtcPending_);
    json += ",\"dtcPermanent\":" + String(remoteDtcPermanent_);
    json += ",\"lastStored\":\"" + remoteLastStored_ + "\"";
    json += ",\"lastPending\":\"" + remoteLastPending_ + "\"";
    json += ",\"lastPermanent\":\"" + remoteLastPermanent_ + "\"";
    json += ",\"udsSummary\":\"" + remoteUdsSummary_ + "\"";
    json += ",\"fordModuleMask\":" + String(remoteFordModuleMask_);
    json += ",\"toyotaModuleMask\":" + String(remoteToyotaModuleMask_);
    json += ",";
    appendFloatOrNull(json, "speed", remoteSpeedValid_, remoteSpeedKph_, 0);
    json += ",";
    appendFloatOrNull(json, "boost", remoteBoostValid_, remoteBoostKpa_, 0);
    json += ",";
    appendFloatOrNull(json, "boostPsi", remoteBoostValid_, remoteBoostKpa_ * kKpaToPsi, 1);
    json += ",";
    appendFloatOrNull(json, "fuel", remoteFuelValid_, remoteFuelPct_, 1);
    json += ",";
    appendFloatOrNull(json, "fuelLiters", remoteFuelValid_, (remoteFuelPct_ * ProjectConfig::kFuelTankLiters) / 100.0f, 1);
    json += ",";
    appendFloatOrNull(json, "throttle", remoteThrottleValid_, remoteThrottlePct_, 1);
    json += ",";
    appendFloatOrNull(json, "dpf", remoteDpfValid_, remoteDpfPct_, 1);
    json += ",";
    appendFloatOrNull(json, "oilPressure", remoteOilPressureValid_, remoteOilPressureKpa_, 0);
    json += ",";
    appendFloatOrNull(json, "afr", remoteAfrValid_, remoteAfr_, 2);
    json += ",";
    appendFloatOrNull(json, "oilTemp", remoteOilTempValid_, remoteOilTempC_, 1);
    json += ",";
    appendFloatOrNull(json, "voltage", remoteVoltageValid_, remoteVoltage_, 2);
    json += ",";
    appendFloatOrNull(json, "atf", remoteAtfValid_, remoteAtfC_, 1);
    json += ",";
    appendFloatOrNull(json, "eg3", remoteEg3Valid_, remoteEg3C_, 1);
    json += ",";
    appendFloatOrNull(json, "o2s1", remoteO2s1Valid_, remoteO2s1V_, 3);
    json += ",";
    appendFloatOrNull(json, "engineSpeedDid", remoteEngineSpeedDidValid_, remoteEngineSpeedDidRpm_, 0);
    json += ",\"rawSeq\":" + String(remoteRawSeq_);
    json += ",\"rawLine\":\"" + remoteRawLine_ + "\"";
    json += ",\"lastCommand\":\"" + lastCommandResult_ + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/diag-cmd", [this]() {
    if (!server.hasArg("c") || server.arg("c").isEmpty()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing c\"}");
      return;
    }

    const char command = server.arg("c")[0];
    String responseBody;
    const bool ok = sendGreatScanCommand(command, responseBody);

    String json = "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";
    json += ",\"command\":\"" + String(command) + "\"";
    json += ",\"response\":\"" + responseBody + "\"";
    json += "}";
    server.send(ok ? 200 : 502, "application/json", json);
  });

  server.on("/api/diag-auto", [this]() {
    if (!server.hasArg("action") || server.arg("action").isEmpty()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing action\"}");
      return;
    }

    const String action = server.arg("action");
    char command = 'o';
    if (action == "read") {
      command = detectedVehicle_ == "TOYOTA" ? 'r' : 'x';
    } else if (action == "clear") {
      command = 'k';
    } else if (action == "scan") {
      command = 'o';
    } else {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid action\"}");
      return;
    }

    String responseBody;
    const bool ok = sendGreatScanCommand(command, responseBody);

    String json = "{";
    json += "\"ok\":";
    json += ok ? "true" : "false";
    json += ",\"action\":\"" + action + "\"";
    json += ",\"oem\":\"" + detectedVehicle_ + "\"";
    json += ",\"command\":\"" + String(command) + "\"";
    json += "}";
    server.send(ok ? 200 : 502, "application/json", json);
  });

  server.onNotFound([]() {
    server.send(204, "text/plain", "");
  });

  server.begin();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WEB] STA IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[GS] Polling: %s\n", ProjectConfig::kGreatScanStatusUrl);
  }
  Serial.printf("[WEB] Ready at http://%s\n", WiFi.localIP().toString().c_str());
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    Serial.printf("[WEB] AP SSID: %s\n", ProjectConfig::kApSsid);
    Serial.printf("[WEB] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  }
}

void WebUi::tick() {
  server.handleClient();
  maintainStationConnection();

  pollGreatScanSerial();
  const bool serialActive = ProjectConfig::kGreatScanSerialBridgeEnabled &&
                            (millis() - lastSerialPacketMs_ <= kSerialTelemetryStaleMs);
  if (serialActive) {
    remoteOnline_ = true;
    return;
  }

  if (pollStatusNext_) {
    pollGreatScanStatus();
  } else {
    pollGreatScanRaw();
  }
  pollStatusNext_ = !pollStatusNext_;

  if (!remoteOnline_) {
    zeroAllGaugeValues();
  }
}

void WebUi::zeroAllGaugeValues() {
  if (rpm_) *rpm_ = 0.0f;
  if (coolantC_) *coolantC_ = 0.0f;
  if (speedKph_) *speedKph_ = 0.0f;
  if (boostKpa_) *boostKpa_ = 0.0f;
  if (fuelPct_) *fuelPct_ = 0.0f;
  if (throttlePct_) *throttlePct_ = 0.0f;
  if (dpfPct_) *dpfPct_ = 0.0f;
  if (oilPressureKpa_) *oilPressureKpa_ = 0.0f;
  if (afr_) *afr_ = 0.0f;
  if (oilTempC_) *oilTempC_ = 0.0f;
  if (voltage_) *voltage_ = 0.0f;
  if (atfC_) *atfC_ = 0.0f;
  if (eg3C_) *eg3C_ = 0.0f;
  if (o2s1V_) *o2s1V_ = 0.0f;
  if (engineSpeedDidRpm_) *engineSpeedDidRpm_ = 0.0f;

  remoteSpeedKph_ = 0.0f;
  remoteBoostKpa_ = 0.0f;
  remoteFuelPct_ = 0.0f;
  remoteThrottlePct_ = 0.0f;
  remoteDpfPct_ = 0.0f;
  remoteOilPressureKpa_ = 0.0f;
  remoteAfr_ = 0.0f;
  remoteOilTempC_ = 0.0f;
  remoteVoltage_ = 0.0f;
  remoteAtfC_ = 0.0f;
  remoteEg3C_ = 0.0f;
  remoteO2s1V_ = 0.0f;
  remoteEngineSpeedDidRpm_ = 0.0f;

  remoteSpeedValid_ = true;
  remoteBoostValid_ = true;
  remoteFuelValid_ = true;
  remoteThrottleValid_ = true;
  remoteDpfValid_ = true;
  remoteOilPressureValid_ = true;
  remoteAfrValid_ = true;
  remoteOilTempValid_ = true;
  remoteVoltageValid_ = true;
  remoteAtfValid_ = true;
  remoteEg3Valid_ = true;
  remoteO2s1Valid_ = true;
  remoteEngineSpeedDidValid_ = true;
  remoteGaugeValid_ = false;
}

void WebUi::maintainStationConnection() {
  if (!stationEnabled_) {
    stationConnected_ = false;
    return;
  }

  stationConnected_ = WiFi.status() == WL_CONNECTED;

  if (stationConnected_) {
    if (!stationConnectedLogged_) {
      Serial.printf("[WEB] STA connected: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("[GS] Polling: %s\n", ProjectConfig::kGreatScanStatusUrl);
      stationConnectedLogged_ = true;
    }
    return;
  }

  stationConnectedLogged_ = false;
  const unsigned long now = millis();
  if (now - lastStationReconnectMs_ < kStaReconnectIntervalMs) {
    return;
  }

  lastStationReconnectMs_ = now;
  WiFi.disconnect();
  WiFi.begin(ProjectConfig::kStaSsid, ProjectConfig::kStaPassword);
  Serial.println("[WEB] STA reconnect attempt...");
}

void WebUi::requestImmediateRefresh() {
  lastRemotePollMs_ = 0;
  lastRemoteRawPollMs_ = 0;
  Serial.println("[WEB] Immediate Great Scan refresh requested");
  pollGreatScanStatus();
  pollGreatScanRaw();
}

bool WebUi::runAutoModuleScan() {
  String responseBody;
  return sendGreatScanCommand('o', responseBody);
}

bool WebUi::runAutoReadDtc() {
  const char command = detectedVehicle_ == "TOYOTA" ? 'r' : 'x';
  String responseBody;
  return sendGreatScanCommand(command, responseBody);
}

bool WebUi::runAutoClearDtc() {
  String responseBody;
  return sendGreatScanCommand('k', responseBody);
}

bool WebUi::isRemoteOnline() const {
  return remoteOnline_;
}

bool WebUi::isTelemetryStale() const {
  if (!remoteOnline_ || !remoteGaugeValid_) {
    return true;
  }

  if (ProjectConfig::kGreatScanSerialBridgeEnabled && lastSerialPacketMs_ > 0) {
    const unsigned long serialAgeMs = millis() - lastSerialPacketMs_;
    if (serialAgeMs <= kSerialTelemetryStaleMs) {
      return false;
    }
  }

  const unsigned long ageMs = millis() - lastRemoteGaugeMs_;
  return ageMs > kTelemetryStaleMs;
}

bool WebUi::isStationEnabled() const {
  return stationEnabled_;
}

bool WebUi::isStationConnected() const {
  return stationConnected_;
}

const char* WebUi::detectedVehicleName() const {
  return detectedVehicle_.c_str();
}

const char* WebUi::lastCommandResult() const {
  return lastCommandResult_.c_str();
}

const char* WebUi::diagnosticSummary() const {
  return remoteUdsSummary_.c_str();
}

void WebUi::pollGreatScanStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    remoteOnline_ = false;
    return;
  }

  const unsigned long now = millis();
  if (now < statusPollBackoffUntilMs_) {
    return;
  }

  if (now - lastRemotePollMs_ < ProjectConfig::kGreatScanStatusPollMs) {
    return;
  }
  lastRemotePollMs_ = now;

  HTTPClient http;
  http.setTimeout(kStatusTimeoutMs);
  if (!http.begin(ProjectConfig::kGreatScanStatusUrl)) {
    remoteOnline_ = false;
    if (statusPollFailureCount_ < 15) {
      ++statusPollFailureCount_;
    }
    statusPollBackoffUntilMs_ = now + scaledBackoffMs(kStatusFailBackoffBaseMs, statusPollFailureCount_);
    return;
  }

  const int code = http.GET();
  if (code != 200) {
    remoteOnline_ = false;
    if (statusPollFailureCount_ < 15) {
      ++statusPollFailureCount_;
    }
    statusPollBackoffUntilMs_ = now + scaledBackoffMs(kStatusFailBackoffBaseMs, statusPollFailureCount_);
    if ((statusPollFailureCount_ % 5U) == 1U) {
      Serial.printf("[GS] Status poll failure http=%d (count=%u)\n", code, statusPollFailureCount_);
    }
    http.end();
    return;
  }

  const String payload = http.getString();
  remoteOnline_ = parseGreatScanStatus(payload);
  if (remoteOnline_) {
    lastRemoteStatusOkMs_ = now;
    statusPollFailureCount_ = 0;
    statusPollBackoffUntilMs_ = 0;
    parseGaugeValuesFromStatus(payload);
    Serial.printf("[GS] diag=%s can1=%s can2=%s dtc=%d\n",
                  remoteDiag_.c_str(),
                  remoteCan1_.c_str(),
                  remoteCan2_.c_str(),
                  remoteDtcCount_);
  }
  http.end();
}

void WebUi::pollGreatScanSerial() {
  if (!ProjectConfig::kGreatScanSerialBridgeEnabled) {
    return;
  }

  while (greatScanSerial.available() > 0) {
    const char c = static_cast<char>(greatScanSerial.read());
    if (c == '\r') {
      continue;
    }

    if (c != '\n') {
      if (serialLineBuffer_.length() < 400) {
        serialLineBuffer_ += c;
      }
      continue;
    }

    if (serialLineBuffer_.startsWith("@GA")) {
      lastCommandResult_ = serialLineBuffer_.substring(3);
      lastCommandResult_.trim();
      Serial.printf("[GS] Serial ack: %s\n", lastCommandResult_.c_str());
      serialLineBuffer_ = "";
      continue;
    }

    if (!serialLineBuffer_.startsWith("@GS{")) {
      serialLineBuffer_ = "";
      continue;
    }

    const String payload = serialLineBuffer_.substring(3);
    serialLineBuffer_ = "";
    ++serialBridgeFrameCount_;

    const bool ok = parseGreatScanStatus(payload);
    if (!ok) {
      Serial.printf("[GS] Serial frame parse failed #%lu\n", serialBridgeFrameCount_);
      continue;
    }

    parseGaugeValuesFromStatus(payload);
    remoteOnline_ = true;
    lastRemoteStatusOkMs_ = millis();
    lastSerialPacketMs_ = lastRemoteStatusOkMs_;
    if ((serialBridgeFrameCount_ % 20UL) == 0UL) {
      Serial.printf("[GS] Serial frame ok count=%lu rpm=%.0f speed=%.0f\n", serialBridgeFrameCount_, rpm_ ? *rpm_ : 0.0f, speedKph_ ? *speedKph_ : 0.0f);
    }
  }
}

bool WebUi::parseGreatScanStatus(const String& payload) {
  String diag;
  String can1;
  String can2;
  String vin;
  int dtc = 0;
  int dtcStored = 0;
  int dtcPending = 0;
  int dtcPermanent = 0;
  int fordMask = 0;
  int toyotaMask = 0;
  String lastStored;
  String lastPending;
  String lastPermanent;
  float boost = 0.0f;
  float dpf = 0.0f;
  float oilPressure = 0.0f;
  float stft = 0.0f;
  float ltft = 0.0f;
  float oilTemp = 0.0f;
  float voltage = 0.0f;
  float atf = 0.0f;
  float eg3 = 0.0f;
  float o2s1 = 0.0f;
  float engineSpeedDid = 0.0f;

  const bool okDiag = extractJsonString(payload, "diag", diag);
  const bool okCan1 = extractJsonString(payload, "can1", can1);
  const bool okCan2 = extractJsonString(payload, "can2", can2);
  const bool okVin = extractJsonString(payload, "vin", vin);
  const bool okDtc = extractJsonInt(payload, "dtc", dtc);
  const bool okStored = extractJsonInt(payload, "dtc_stored", dtcStored);
  const bool okPending = extractJsonInt(payload, "dtc_pending", dtcPending);
  const bool okPermanent = extractJsonInt(payload, "dtc_permanent", dtcPermanent);
  const bool okFordMask = extractJsonInt(payload, "ford_module_mask", fordMask);
  const bool okToyotaMask = extractJsonInt(payload, "toyota_module_mask", toyotaMask);
  const bool okLastStored = extractJsonString(payload, "last_stored", lastStored);
  const bool okLastPending = extractJsonString(payload, "last_pending", lastPending);
  const bool okLastPermanent = extractJsonString(payload, "last_permanent", lastPermanent);
  const bool okBoost = extractJsonFloat(payload, "boost", boost);
  const bool okDpf = extractJsonFloat(payload, "dpf", dpf);
  const bool okOilPressure = extractJsonFloat(payload, "oil_pressure", oilPressure);
  const bool okFuelPressure = extractJsonFloat(payload, "fuel_pressure", oilPressure);
  const bool okStft = extractJsonFloat(payload, "stft_b1", stft);
  const bool okLtft = extractJsonFloat(payload, "ltft_b1", ltft);
  const bool okOilTemp = extractJsonFloat(payload, "oil_temp", oilTemp);
  const bool okVoltage = extractJsonFloat(payload, "voltage", voltage);
  const bool okAtf = extractJsonFloat(payload, "atf", atf);
  const bool okEg3 = extractJsonFloat(payload, "eg3", eg3);
  const bool okO2s1 = extractJsonFloat(payload, "o2s1", o2s1);
  const bool okEngineSpeedDid = extractJsonFloat(payload, "engine_speed_did", engineSpeedDid);

  bool parsedAny = false;

  if (okDiag) {
    remoteDiag_ = diag;
    parsedAny = true;
  }
  if (okCan1) {
    remoteCan1_ = can1;
    lastCan1UpdateMs_ = millis();
    parsedAny = true;
  }
  if (okCan2) {
    remoteCan2_ = can2;
    lastCan2UpdateMs_ = millis();
    parsedAny = true;
  }
  if (okVin) {
    remoteVin_ = vin;
    parsedAny = true;
  }
  if (okDtc) {
    remoteDtcCount_ = dtc;
    parsedAny = true;
  }
  if (okStored) {
    remoteDtcStored_ = dtcStored;
    parsedAny = true;
  }
  if (okPending) {
    remoteDtcPending_ = dtcPending;
    parsedAny = true;
  }
  if (okPermanent) {
    remoteDtcPermanent_ = dtcPermanent;
    parsedAny = true;
  }
  if (okFordMask) {
    remoteFordModuleMask_ = fordMask;
    parsedAny = true;
  }
  if (okToyotaMask) {
    remoteToyotaModuleMask_ = toyotaMask;
    parsedAny = true;
  }
  if (okLastStored) {
    remoteLastStored_ = lastStored;
    parsedAny = true;
  }
  if (okLastPending) {
    remoteLastPending_ = lastPending;
    parsedAny = true;
  }
  if (okLastPermanent) {
    remoteLastPermanent_ = lastPermanent;
    parsedAny = true;
  }

  remoteBoostValid_ = okBoost;
  if (okBoost) {
    remoteBoostKpa_ = boost;
    parsedAny = true;
  }
  remoteDpfValid_ = okDpf;
  if (okDpf) {
    remoteDpfPct_ = dpf;
    parsedAny = true;
  }

  remoteOilPressureValid_ = okOilPressure || okFuelPressure;
  if (remoteOilPressureValid_) {
    remoteOilPressureKpa_ = oilPressure;
    parsedAny = true;
  }

  remoteAfrValid_ = okStft || okLtft;
  if (remoteAfrValid_) {
    const float trimPct = (okStft ? stft : 0.0f) + (okLtft ? ltft : 0.0f);
    remoteAfr_ = 14.7f * (1.0f + (trimPct / 100.0f));
    parsedAny = true;
  }

  remoteOilTempValid_ = okOilTemp;
  if (okOilTemp) {
    remoteOilTempC_ = oilTemp;
    parsedAny = true;
  }

  remoteVoltageValid_ = okVoltage;
  if (okVoltage) {
    remoteVoltage_ = voltage;
    parsedAny = true;
  }

  remoteAtfValid_ = okAtf;
  if (okAtf) {
    remoteAtfC_ = atf;
    parsedAny = true;
  }

  remoteEg3Valid_ = okEg3;
  if (okEg3) {
    remoteEg3C_ = eg3;
    parsedAny = true;
  }

  remoteO2s1Valid_ = okO2s1;
  if (okO2s1) {
    remoteO2s1V_ = o2s1;
    parsedAny = true;
  }

  remoteEngineSpeedDidValid_ = okEngineSpeedDid;
  if (okEngineSpeedDid) {
    remoteEngineSpeedDidRpm_ = engineSpeedDid;
    parsedAny = true;
  }

  if (oilPressureKpa_ && remoteOilPressureValid_) {
    *oilPressureKpa_ = remoteOilPressureKpa_;
  }
  if (afr_ && remoteAfrValid_) {
    *afr_ = remoteAfr_;
  }
  if (oilTempC_ && remoteOilTempValid_) {
    *oilTempC_ = remoteOilTempC_;
  }
  if (voltage_ && remoteVoltageValid_) {
    *voltage_ = remoteVoltage_;
  }
  if (atfC_ && remoteAtfValid_) {
    *atfC_ = remoteAtfC_;
  }
  if (eg3C_ && remoteEg3Valid_) {
    *eg3C_ = remoteEg3C_;
  }
  if (o2s1V_ && remoteO2s1Valid_) {
    *o2s1V_ = remoteO2s1V_;
  }
  if (engineSpeedDidRpm_ && remoteEngineSpeedDidValid_) {
    *engineSpeedDidRpm_ = remoteEngineSpeedDidRpm_;
  }

  refreshDiagnosticSummaryFromStatus();
  updateDetectedVehicle();
  return parsedAny;
}

void WebUi::pollGreatScanRaw() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (now < rawPollBackoffUntilMs_) {
    return;
  }

  if (now - lastRemoteRawPollMs_ < ProjectConfig::kGreatScanRawPollMs) {
    return;
  }
  lastRemoteRawPollMs_ = now;

  HTTPClient http;
  http.setTimeout(kRawTimeoutMs);
  if (!http.begin(ProjectConfig::kGreatScanRawUrl)) {
    if (rawPollFailureCount_ < 15) {
      ++rawPollFailureCount_;
    }
    rawPollBackoffUntilMs_ = now + scaledBackoffMs(kRawFailBackoffBaseMs, rawPollFailureCount_);
    return;
  }

  const int code = http.GET();
  if (code != 200) {
    if (rawPollFailureCount_ < 15) {
      ++rawPollFailureCount_;
    }
    rawPollBackoffUntilMs_ = now + scaledBackoffMs(kRawFailBackoffBaseMs, rawPollFailureCount_);
    if ((rawPollFailureCount_ % 5U) == 1U) {
      Serial.printf("[GS] Raw poll failure http=%d (count=%u)\n", code, rawPollFailureCount_);
    }
    http.end();
    return;
  }

  const String payload = http.getString();
  if (parseGreatScanRaw(payload)) {
    rawPollFailureCount_ = 0;
    rawPollBackoffUntilMs_ = 0;
    if (remoteRawSeq_ != lastLoggedRawSeq_) {
      lastLoggedRawSeq_ = remoteRawSeq_;
      Serial.printf("[GS-RAW] #%lu %s\n", remoteRawSeq_, remoteRawLine_.c_str());
    }
  }
  http.end();
}

bool WebUi::parseGreatScanRaw(const String& payload) {
  String line;
  int seq = 0;

  const bool okLine = extractJsonString(payload, "line", line);
  const bool okSeq = extractJsonInt(payload, "seq", seq);
  if (!(okLine && okSeq)) {
    return false;
  }

  remoteRawLine_ = line;
  remoteRawSeq_ = static_cast<unsigned long>(seq);
  parseGaugeValuesFromRawLine(line);
  refreshDiagnosticSummaryFromRawLine(line);
  updateDetectedVehicle();
  return true;
}

void WebUi::refreshDiagnosticSummaryFromStatus() {
  if (remoteDtcStored_ <= 0 && remoteDtcPending_ <= 0 && remoteDtcPermanent_ <= 0) {
    remoteUdsSummary_ = "DTC clear";
    return;
  }

  String summary = "S/P/P ";
  summary += String(remoteDtcStored_);
  summary += "/";
  summary += String(remoteDtcPending_);
  summary += "/";
  summary += String(remoteDtcPermanent_);

  if (remoteLastStored_ != "-") {
    summary += " st=";
    summary += remoteLastStored_;
  } else if (remoteLastPending_ != "-") {
    summary += " pe=";
    summary += remoteLastPending_;
  } else if (remoteLastPermanent_ != "-") {
    summary += " pm=";
    summary += remoteLastPermanent_;
  }

  remoteUdsSummary_ = trimForUi(summary, 46);
}

void WebUi::refreshDiagnosticSummaryFromRawLine(const String& line) {
  if (line.indexOf("UDS 0x19") < 0) {
    return;
  }

  int fromPos = line.indexOf(" from ");
  int dtcsPos = line.indexOf(" dtcs=");
  int latestPos = line.indexOf(" latest=");
  if (fromPos < 0 || dtcsPos < 0) {
    return;
  }

  const int moduleStart = fromPos + 6;
  String module = line.substring(moduleStart, dtcsPos);
  module.trim();

  String count = "?";
  const int countStart = dtcsPos + 6;
  int countEnd = countStart;
  while (countEnd < line.length() && isDigit(line[countEnd])) {
    countEnd++;
  }
  if (countEnd > countStart) {
    count = line.substring(countStart, countEnd);
  }

  String summary = module + " dtc=" + count;
  if (latestPos >= 0) {
    String latest = line.substring(latestPos + 8);
    latest.trim();
    if (!latest.isEmpty() && latest != "-") {
      summary += " ";
      summary += latest;
    }
  }

  remoteUdsSummary_ = trimForUi(summary, 46);
}

bool WebUi::parseGaugeValuesFromStatus(const String& payload) {
  float rpm = 0.0f;
  float speed = 0.0f;
  float boost = 0.0f;
  float coolant = 0.0f;
  float fuel = 0.0f;
  float throttle = 0.0f;
  float dpf = 0.0f;
  float atf = 0.0f;
  float eg3 = 0.0f;
  float o2s1 = 0.0f;
  float engineSpeedDid = 0.0f;

  const bool hasRpm = extractJsonFloat(payload, "rpm", rpm);
  const bool hasSpeed = extractJsonFloat(payload, "speed", speed);
  const bool hasBoost = extractJsonFloat(payload, "boost", boost);
  const bool hasCoolant = extractJsonFloat(payload, "coolant", coolant);
  const bool hasFuel = extractJsonFloat(payload, "fuel", fuel);
  const bool hasThrottle = extractJsonFloat(payload, "throttle", throttle);
  const bool hasDpf = extractJsonFloat(payload, "dpf", dpf);
  const bool hasAtf = extractJsonFloat(payload, "atf", atf);
  const bool hasEg3 = extractJsonFloat(payload, "eg3", eg3);
  const bool hasO2s1 = extractJsonFloat(payload, "o2s1", o2s1);
  const bool hasEngineSpeedDid = extractJsonFloat(payload, "engine_speed_did", engineSpeedDid);

  bool updatedAny = false;

  if (hasRpm && rpm_) {
    *rpm_ = rpm;
    updatedAny = true;
  }
  if (hasCoolant && coolantC_) {
    *coolantC_ = coolant;
    updatedAny = true;
  }
  if (hasSpeed) {
    if (speedKph_) *speedKph_ = speed;
    remoteSpeedKph_ = speed;
    remoteSpeedValid_ = true;
    updatedAny = true;
  }
  if (hasBoost) {
    if (boostKpa_) *boostKpa_ = boost;
    remoteBoostKpa_ = boost;
    remoteBoostValid_ = true;
    updatedAny = true;
  }
  if (hasFuel) {
    if (fuelPct_) *fuelPct_ = fuel;
    remoteFuelPct_ = fuel;
    remoteFuelValid_ = true;
    updatedAny = true;
  }
  if (hasThrottle) {
    if (throttlePct_) *throttlePct_ = throttle;
    remoteThrottlePct_ = throttle;
    remoteThrottleValid_ = true;
    updatedAny = true;
  }
  if (hasDpf) {
    if (dpfPct_) *dpfPct_ = dpf;
    remoteDpfPct_ = dpf;
    remoteDpfValid_ = true;
    updatedAny = true;
  }
  if (hasAtf) {
    if (atfC_) *atfC_ = atf;
    remoteAtfC_ = atf;
    remoteAtfValid_ = true;
    updatedAny = true;
  }
  if (hasEg3) {
    if (eg3C_) *eg3C_ = eg3;
    remoteEg3C_ = eg3;
    remoteEg3Valid_ = true;
    updatedAny = true;
  }
  if (hasO2s1) {
    if (o2s1V_) *o2s1V_ = o2s1;
    remoteO2s1V_ = o2s1;
    remoteO2s1Valid_ = true;
    updatedAny = true;
  }
  if (hasEngineSpeedDid) {
    if (engineSpeedDidRpm_) *engineSpeedDidRpm_ = engineSpeedDid;
    remoteEngineSpeedDidRpm_ = engineSpeedDid;
    remoteEngineSpeedDidValid_ = true;
    updatedAny = true;
  }

  if (updatedAny) {
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
  }

  return updatedAny;
}

bool WebUi::parseGaugeValuesFromRawLine(const String& line) {
  float value = 0.0f;

  if ((line.indexOf("RPM") >= 0) && (std::sscanf(line.c_str(), "%*[^0-9]%f", &value) == 1)) {
    if (rpm_) *rpm_ = value;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Coolant") >= 0 || line.indexOf("coolant") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (coolantC_) *coolantC_ = value;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Vehicle Speed") >= 0 || line.indexOf("Speed") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9]%f", &value) == 1)) {
    if (speedKph_) *speedKph_ = value;
    remoteSpeedKph_ = value;
    remoteSpeedValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Fuel Level") >= 0 || line.indexOf("Fuel") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9]%f", &value) == 1)) {
    if (fuelPct_) *fuelPct_ = value;
    remoteFuelPct_ = value;
    remoteFuelValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Throttle") >= 0) && (std::sscanf(line.c_str(), "%*[^0-9]%f", &value) == 1)) {
    if (throttlePct_) *throttlePct_ = value;
    remoteThrottlePct_ = value;
    remoteThrottleValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Boost") >= 0 || line.indexOf("BOOST") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (boostKpa_) *boostKpa_ = value;
    remoteBoostKpa_ = value;
    remoteBoostValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("DPF") >= 0 || line.indexOf("DEF") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (dpfPct_) *dpfPct_ = value;
    remoteDpfPct_ = value;
    remoteDpfValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Oil Pressure") >= 0 || line.indexOf("OIL P") >= 0 || line.indexOf("Fuel Pressure") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (oilPressureKpa_) *oilPressureKpa_ = value;
    remoteOilPressureKpa_ = value;
    remoteOilPressureValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("AFR") >= 0 || line.indexOf("A/F") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (afr_) *afr_ = value;
    remoteAfr_ = value;
    remoteAfrValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Oil Temp") >= 0 || line.indexOf("OIL T") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (oilTempC_) *oilTempC_ = value;
    remoteOilTempC_ = value;
    remoteOilTempValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  if ((line.indexOf("Volt") >= 0 || line.indexOf("Vbat") >= 0 || line.indexOf("VBAT") >= 0) &&
      (std::sscanf(line.c_str(), "%*[^0-9-]%f", &value) == 1)) {
    if (voltage_) *voltage_ = value;
    remoteVoltage_ = value;
    remoteVoltageValid_ = true;
    lastRemoteGaugeMs_ = millis();
    remoteGaugeValid_ = true;
    return true;
  }

  return false;
}

bool WebUi::sendGreatScanCommand(const char command, String& responseBody) {
  responseBody = "offline";

  if (ProjectConfig::kGreatScanSerialBridgeEnabled) {
    greatScanSerial.print("@GC");
    greatScanSerial.print(command);
    greatScanSerial.print('\n');
    lastCommandResult_ = String("serial ") + String(command) + " queued";
    responseBody = "serial";
    Serial.printf("[GS] Serial cmd queued: %c\n", command);
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastCommandResult_ = "offline";
    return false;
  }

  String url = ProjectConfig::kGreatScanCommandUrl;
  url += String(command);

  HTTPClient http;
  http.setTimeout(kCommandTimeoutMs);
  if (!http.begin(url)) {
    lastCommandResult_ = "http begin failed";
    return false;
  }

  const int code = http.GET();
  if (code == 200) {
    responseBody = http.getString();
    lastCommandResult_ = String(command) + " ok";
    Serial.printf("[GS] Command %c -> %s\n", command, responseBody.c_str());
    requestImmediateRefresh();
    http.end();
    return true;
  }

  responseBody = String("http ") + String(code);
  lastCommandResult_ = String(command) + " failed";
  Serial.printf("[GS] Command %c failed http=%d\n", command, code);
  http.end();
  return false;
}

void WebUi::updateDetectedVehicle() {
  const String upperRaw = remoteRawLine_;

  if (remoteToyotaModuleMask_ > 0) {
    detectedVehicle_ = "TOYOTA";
    return;
  }

  if (remoteFordModuleMask_ > 0) {
    detectedVehicle_ = "FORD";
    return;
  }

  if (upperRaw.indexOf("[TOYOTA]") >= 0 || upperRaw.indexOf("TOYOTA") >= 0) {
    detectedVehicle_ = "TOYOTA";
    return;
  }

  if (upperRaw.indexOf("[FORD]") >= 0 || upperRaw.indexOf("FORD") >= 0) {
    detectedVehicle_ = "FORD";
    return;
  }

  if (remoteDiag_.equalsIgnoreCase("can1") || remoteDiag_.equalsIgnoreCase("can2")) {
    detectedVehicle_ = "AUTO";
    return;
  }

  detectedVehicle_ = "AUTO";
}
