#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

// ==================== WIFI CONFIGURATION ====================
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "CHANGE_ME_123";
const char *AP_SSID = "ESP32_Servo_Drive";
const char *AP_PASS = "CHANGE_ME_123";

// ==================== HARDWARE PINS =========================
const int PIN_SDA = 21;
const int PIN_SCL = 5;
const int forward = 23; // TA6586 BI / FI (PWM Channel 0)
const int reverse = 22; // TA6586 FI / BI (PWM Channel 1)

// ==================== PWM CONFIGURATION =====================
// 19.5 kHz @ 12-bit (0-4095) gives high resolution & ultrasonic operation
const int PWM_FREQ = 19500;
const int PWM_RES = 12;
const int MAX_PWM = (1 << PWM_RES) - 1; // 4095

// ==================== MT6701 I2C CONFIG =====================
const uint8_t MT6701_ADDR = 0x06;
const uint8_t MT6701_REG_ANG = 0x03;

// ==================== SHARED STATE VARIABLES ================
Preferences prefs;
WebServer server(80);
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Saved Configuration (Tuned for ~1.28 deg output-shaft backlash)
float kp = 300.0f;
float ki = 0.5f;
float kd = 10.0f;           // Electronic braking against backlash momentum
bool enP = true;
bool enI = false;          // Kept OFF initially to prevent windup/slamming across gear gap
bool enD = true;
bool motorEnabled = false; // Safe startup: motor disabled by default
bool invertMotor = false;  // Auto-detected or flipped in UI
int minPwmOffset = 500;    // Deadzone stiction compensation (0 - 4095)
float deadbandDeg = 0.70f; // Set to ~half backlash (0.64 deg) to stop hunting chatter

// Angle & Tracking State
int32_t rotationCount = 0;
uint16_t prevRawCounts = 0;
float zeroContinuousAngle = 0.0f;

volatile float sharedRawAngle = 0.0f;
volatile float sharedRealAngle = 0.0f;
volatile float sharedTargetAngle = 0.0f;
volatile int sharedPwmOut = 0;
volatile bool reqZeroCalib = false;
volatile bool i2cError = false;

// PID Internal State
float integral = 0.0f;
float prevRealAngle = 0.0f;
float filteredDerivative = 0.0f;

// ==================== HTML / SVG WEB UI =====================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 MT6701 Servo Drive</title>
<style>
  :root { --bg: #0f172a; --card: #1e293b; --accent: #38bdf8; --danger: #ef4444; --ok: #22c55e; --text: #f8fafc; }
  body { font-family: system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 16px; display: flex; flex-direction: column; align-items: center; }
  h2 { margin: 4px 0 14px 0; font-weight: 600; color: var(--accent); }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(330px, 1fr)); gap: 16px; width: 100%; max-width: 900px; }
  .card { background: var(--card); border-radius: 12px; padding: 18px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }
  .stat-row { display: flex; justify-content: space-between; margin: 8px 0; font-size: 1.05rem; border-bottom: 1px solid #334155; padding-bottom: 6px; }
  .val { font-family: monospace; font-weight: bold; color: var(--accent); font-size: 1.15rem; }
  .dial-box { display: flex; flex-direction: column; align-items: center; user-select: none; }
  svg { width: 270px; height: 160px; overflow: visible; cursor: pointer; touch-action: none; }
  .btn { padding: 10px 14px; border: none; border-radius: 8px; font-weight: 600; cursor: pointer; color: #fff; transition: 0.15s; width: 100%; margin-top: 8px; font-size: 0.95rem; }
  .btn-primary { background: #0284c7; } .btn-primary:hover { background: #0369a1; }
  .btn-danger { background: var(--danger); }
  .btn-ok { background: var(--ok); }
  .pid-row { display: grid; grid-template-columns: 75px 1fr 70px; gap: 8px; align-items: center; margin-bottom: 10px; }
  input[type=number] { width: 100%; padding: 8px; border-radius: 6px; border: 1px solid #475569; background: #0f172a; color: #fff; font-size: 0.95rem; box-sizing: border-box; }
  .toggle { padding: 8px; text-align: center; border-radius: 6px; cursor: pointer; font-weight: bold; font-size: 0.85rem; }
  .on { background: var(--ok); color: #000; }
  .off { background: #475569; color: #cbd5e1; }
</style>
</head>
<body>
  <h2>ESP32 + MT6701 Closed-Loop Servo</h2>
  <div class="grid">
    <!-- DIAL & TELEMETRY CARD -->
    <div class="card dial-box">
      <svg id="dialSvg" viewBox="0 0 240 140">
        <!-- Background Arc (0 to 180 deg) -->
        <path d="M 20 120 A 100 100 0 0 1 220 120" fill="none" stroke="#334155" stroke-width="14" stroke-linecap="round"/>
        <!-- Real Angle Indicator Line -->
        <line id="realLine" x1="120" y1="120" x2="20" y2="120" stroke="#22c55e" stroke-width="4" stroke-linecap="round"/>
        <!-- Target Handle -->
        <circle id="targetKnob" cx="20" cy="120" r="11" fill="#38bdf8" stroke="#fff" stroke-width="2"/>
        <text x="15" y="138" fill="#94a3b8" font-size="11">0°</text>
        <text x="112" y="12" fill="#94a3b8" font-size="11">90°</text>
        <text x="208" y="138" fill="#94a3b8" font-size="11">180°</text>
      </svg>
      <div style="width:100%; margin-top:8px;">
        <div class="stat-row"><span>Target Angle:</span><span class="val"><span id="tVal">0.0</span>°</span></div>
        <div class="stat-row"><span>Real Angle (Multi-turn):</span><span class="val" style="color:#22c55e"><span id="rVal">0.00</span>°</span></div>
        <div class="stat-row"><span>MT6701 Raw Angle:</span><span class="val" style="color:#f59e0b"><span id="rawVal">0.00</span>°</span></div>
        <div class="stat-row"><span>PWM Output (-4095..4095):</span><span class="val" id="pwmVal">0</span></div>
        <div class="stat-row"><span>Sensor Status:</span><span class="val" id="i2cVal">OK</span></div>
      </div>
      <button id="pwrBtn" class="btn btn-danger" onclick="toggleMotor()">MOTOR DISABLED (CLICK TO ARM)</button>
      <button class="btn btn-primary" onclick="setZero()">Calibrate Current Position as 0°</button>
    </div>

    <!-- PID & DRIVE SETTINGS CARD -->
    <div class="card">
      <h3 style="margin-top:0">PID & Drive Configuration</h3>
      <div class="pid-row">
        <span><b>Kp</b></span>
        <input type="number" step="0.1" id="kpIn">
        <div id="enPBtn" class="toggle on" onclick="tog('P')">ON</div>
      </div>
      <div class="pid-row">
        <span><b>Ki</b></span>
        <input type="number" step="0.05" id="kiIn">
        <div id="enIBtn" class="toggle on" onclick="tog('I')">ON</div>
      </div>
      <div class="pid-row">
        <span><b>Kd</b></span>
        <input type="number" step="0.01" id="kdIn">
        <div id="enDBtn" class="toggle on" onclick="tog('D')">ON</div>
      </div>
      <div class="pid-row">
        <span><b>Min PWM</b></span>
        <input type="number" step="10" id="minPwmIn" title="Deadzone compensation (0-2000)">
        <span style="font-size:0.8rem;color:#94a3b8">Stiction</span>
      </div>
      <div class="pid-row">
        <span><b>Deadband</b></span>
        <input type="number" step="0.05" id="dbIn" title="Error tolerance in degrees">
        <span style="font-size:0.8rem;color:#94a3b8">Degrees</span>
      </div>
      <div class="pid-row">
        <span><b>Direction</b></span>
        <div id="invBtn" class="toggle off" style="grid-column: span 2;" onclick="togInv()">NORMAL POLARITY</div>
      </div>
      <button class="btn" style="background:#475569;" onclick="testPolarity()">Auto-Detect Polarity (Test Pulse)</button>
      <button class="btn btn-primary" onclick="savePid()">Apply & Save to EEPROM</button>
      <div id="saveMsg" style="text-align:center; font-size:0.85rem; color:#22c55e; margin-top:6px; height:18px;"></div>
    </div>
  </div>

<script>
let state = { enP:true, enI:true, enD:true, motor:false, inv:false, target:0 };
const svg = document.getElementById('dialSvg');
const knob = document.getElementById('targetKnob');
const realLine = document.getElementById('realLine');
let dragging = false;

function angleToCoords(deg) {
  let rad = (180 - deg) * Math.PI / 180;
  return { x: 120 + 100 * Math.cos(rad), y: 120 - 100 * Math.sin(rad) };
}

function updateDialUI(targetDeg, realDeg) {
  let tC = angleToCoords(Math.max(0, Math.min(180, targetDeg)));
  knob.setAttribute('cx', tC.x);
  knob.setAttribute('cy', tC.y);
  let clampedReal = Math.max(-15, Math.min(195, realDeg));
  let rC = angleToCoords(clampedReal);
  realLine.setAttribute('x2', rC.x);
  realLine.setAttribute('y2', rC.y);
}

function handlePointer(e) {
  if (!dragging) return;
  const rect = svg.getBoundingClientRect();
  const clientX = e.touches ? e.touches[0].clientX : e.clientX;
  const clientY = e.touches ? e.touches[0].clientY : e.clientY;
  const x = (clientX - rect.left) * (240 / rect.width) - 120;
  const y = 120 - (clientY - rect.top) * (140 / rect.height);
  let deg = 180 - (Math.atan2(y, x) * 180 / Math.PI);
  if (deg > 180) deg = (x < 0) ? 0 : 180;
  deg = Math.max(0, Math.min(180, deg));
  state.target = deg;
  document.getElementById('tVal').innerText = deg.toFixed(1);
  updateDialUI(state.target, parseFloat(document.getElementById('rVal').innerText) || 0);
  fetch(`/setTarget?val=${deg.toFixed(2)}`);
}

svg.addEventListener('mousedown', (e) => { dragging = true; handlePointer(e); });
window.addEventListener('mousemove', handlePointer);
window.addEventListener('mouseup', () => dragging = false);
svg.addEventListener('touchstart', (e) => { dragging = true; handlePointer(e); e.preventDefault(); }, {passive:false});
window.addEventListener('touchmove', handlePointer, {passive:false});
window.addEventListener('touchend', () => dragging = false);

function renderToggles() {
  ['P','I','D'].forEach(k => {
    let el = document.getElementById('en'+k+'Btn');
    let on = state['en'+k];
    el.className = 'toggle ' + (on ? 'on' : 'off');
    el.innerText = on ? 'ON' : 'OFF';
  });
  let pwr = document.getElementById('pwrBtn');
  pwr.className = 'btn ' + (state.motor ? 'btn-ok' : 'btn-danger');
  pwr.innerText = state.motor ? 'MOTOR ARMED (CLICK TO STOP)' : 'MOTOR DISABLED (CLICK TO ARM)';
  let inv = document.getElementById('invBtn');
  inv.className = 'toggle ' + (state.inv ? 'on' : 'off');
  inv.innerText = state.inv ? 'INVERTED POLARITY' : 'NORMAL POLARITY';
}

function tog(k) { state['en'+k] = !state['en'+k]; renderToggles(); savePid(); }
function togInv() { state.inv = !state.inv; renderToggles(); savePid(); }
function toggleMotor() {
  state.motor = !state.motor;
  renderToggles();
  fetch(`/setMotor?en=${state.motor?1:0}`);
}

function setZero() {
  fetch('/setZero').then(() => {
    state.target = 0;
    document.getElementById('tVal').innerText = '0.0';
  });
}

function testPolarity() {
  fetch('/testPolarity').then(r => r.text()).then(t => {
    alert(t);
    loadConfig();
  });
}

function savePid() {
  const q = new URLSearchParams({
    kp: document.getElementById('kpIn').value,
    ki: document.getElementById('kiIn').value,
    kd: document.getElementById('kdIn').value,
    enP: state.enP?1:0, enI: state.enI?1:0, enD: state.enD?1:0,
    minPwm: document.getElementById('minPwmIn').value,
    db: document.getElementById('dbIn').value,
    inv: state.inv?1:0
  });
  fetch('/setPid?' + q.toString()).then(() => {
    const m = document.getElementById('saveMsg');
    m.innerText = 'Saved to Flash!';
    setTimeout(() => m.innerText = '', 1800);
  });
}

function loadConfig() {
  fetch('/config').then(r => r.json()).then(d => {
    document.getElementById('kpIn').value = d.kp;
    document.getElementById('kiIn').value = d.ki;
    document.getElementById('kdIn').value = d.kd;
    document.getElementById('minPwmIn').value = d.minPwm;
    document.getElementById('dbIn').value = d.db;
    state.enP = d.enP; state.enI = d.enI; state.enD = d.enD;
    state.motor = d.motor; state.inv = d.inv; state.target = d.target;
    document.getElementById('tVal').innerText = d.target.toFixed(1);
    renderToggles();
  });
}

setInterval(() => {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('rawVal').innerText = d.raw.toFixed(2);
    document.getElementById('rVal').innerText = d.real.toFixed(2);
    document.getElementById('pwmVal').innerText = d.pwm;
    document.getElementById('i2cVal').innerText = d.err ? 'I2C ERROR!' : 'OK';
    document.getElementById('i2cVal').style.color = d.err ? '#ef4444' : '#22c55e';
    if (!dragging) {
      state.target = d.target;
      document.getElementById('tVal').innerText = d.target.toFixed(1);
    }
    updateDialUI(state.target, d.real);
  }).catch(()=>{});
}, 100);

window.onload = loadConfig;
</script>
</body>
</html>
)rawliteral";

// ==================== HARDWARE ABSTRACTION ==================
// Compatible with both ESP32 Arduino Core 2.x and 3.x
void initPWM() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(forward, PWM_FREQ, PWM_RES);
  ledcAttach(reverse, PWM_FREQ, PWM_RES);
#else
  ledcSetup(0, PWM_FREQ, PWM_RES);
  ledcSetup(1, PWM_FREQ, PWM_RES);
  ledcAttachPin(forward, 0);
  ledcAttachPin(reverse, 1);
#endif
}

void writePWM(int pwmVal) {
  if (invertMotor)
    pwmVal = -pwmVal;
  pwmVal = constrain(pwmVal, -MAX_PWM, MAX_PWM);

  uint32_t dutyFwd = 0;
  uint32_t dutyRev = 0;

  if (pwmVal > 0) {
    dutyFwd = pwmVal;
  } else if (pwmVal < 0) {
    dutyRev = -pwmVal;
  }

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(forward, dutyFwd);
  ledcWrite(reverse, dutyRev);
#else
  ledcWrite(0, dutyFwd);
  ledcWrite(1, dutyRev);
#endif
}

// Read 14-bit raw angle (0..16383) from MT6701 over I2C
bool readMT6701Counts(uint16_t &counts) {
  Wire.beginTransmission(MT6701_ADDR);
  Wire.write(MT6701_REG_ANG);
  if (Wire.endTransmission(false) != 0)
    return false;

  if (Wire.requestFrom(MT6701_ADDR, (uint8_t)2) != 2)
    return false;
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  // Angle is 14-bit: Reg 0x03[7:0] = Angle[13:6], Reg 0x04[7:2] = Angle[5:0]
  counts = ((uint16_t)msb << 6) | (lsb >> 2);
  return true;
}

// ==================== CORE 1: 1000Hz PID TASK ===============
void pidLoopTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1); // 1 ms = 1000 Hz
  const float dt = 0.001f;

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    uint16_t rawCounts = 0;
    if (!readMT6701Counts(rawCounts)) {
      i2cError = true;
      writePWM(0); // Fail-safe: cut motor power if encoder wire disconnects
      continue;
    }
    i2cError = false;

    // --- 0 <-> 360 Wrap-around Detection (in 14-bit counts: 0..16383) ---
    int32_t diff = (int32_t)rawCounts - (int32_t)prevRawCounts;
    if (diff < -8192) {
      rotationCount++; // Crossed from 360 -> 0 (increasing angle)
    } else if (diff > 8192) {
      rotationCount--; // Crossed from 0 -> 360 (decreasing angle)
    }
    prevRawCounts = rawCounts;

    float rawDeg = (float)rawCounts * (360.0f / 16384.0f);
    float continuousAngle = (rotationCount * 360.0f) + rawDeg;

    // Handle calibration request from Web UI
    if (reqZeroCalib) {
      zeroContinuousAngle = continuousAngle;
      integral = 0.0f;
      sharedTargetAngle = 0.0f;
      reqZeroCalib = false;
      // Save new raw zero offset to flash (normalized 0..360)
      prefs.putFloat("zeroOff", rawDeg);
      prefs.putBool("hasZero", true);
    }

    float realAngle = continuousAngle - zeroContinuousAngle;
    float target = sharedTargetAngle;

    // --- Advanced PID Calculation ---
    float error = target - realAngle;
    int pwmCommand = 0;

    if (!motorEnabled || fabs(error) <= deadbandDeg) {
      pwmCommand = 0;
      // Decay integral gently inside deadband to prevent hunting
      integral *= 0.95f;
    } else {
      // 1. Proportional Term
      float pTerm = enP ? (kp * error) : 0.0f;

      // 2. Integral Term with Clamping Anti-Windup
      if (enI && ki > 0.0001f) {
        integral += error * ki * dt;
        integral = constrain(integral, -(float)MAX_PWM, (float)MAX_PWM);
      } else {
        integral = 0.0f;
      }
      float iTerm = enI ? integral : 0.0f;

      // 3. Derivative on Measurement (avoids derivative kick on target change)
      // Includes low-pass filter (alpha = 0.15) to suppress encoder
      // quantization noise
      float rawDeriv = -(realAngle - prevRealAngle) / dt;
      filteredDerivative = 0.85f * filteredDerivative + 0.15f * rawDeriv;
      float dTerm = enD ? (kd * filteredDerivative) : 0.0f;

      float pidSum = pTerm + iTerm + dTerm;

      // Apply Stiction / Deadzone Compensation
      if (pidSum > 0.1f) {
        pwmCommand = (int)(pidSum + minPwmOffset);
      } else if (pidSum < -0.1f) {
        pwmCommand = (int)(pidSum - minPwmOffset);
      }
      pwmCommand = constrain(pwmCommand, -MAX_PWM, MAX_PWM);
    }

    writePWM(pwmCommand);

    // Update shared telemetry for Web UI
    sharedRawAngle = rawDeg;
    sharedRealAngle = realAngle;
    sharedPwmOut = pwmCommand;
    prevRealAngle = realAngle;
  }
}

// ==================== SETUP & MAIN LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n--- ESP32 MT6701 Servo Drive Starting ---");

  // Load saved preferences
  prefs.begin("servo", false);
  kp = prefs.getFloat("kp", kp);
  ki = prefs.getFloat("ki", ki);
  kd = prefs.getFloat("kd", kd);
  enP = prefs.getBool("enP", enP);
  enI = prefs.getBool("enI", enI);
  enD = prefs.getBool("enD", enD);
  minPwmOffset = prefs.getInt("minPwm", minPwmOffset);
  deadbandDeg = prefs.getFloat("db", deadbandDeg);
  invertMotor = prefs.getBool("inv", invertMotor);

  // Initialize Motor PWM
  initPWM();
  writePWM(0);

  // Initialize I2C for MT6701
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  // Read initial encoder counts
  uint16_t initialCounts = 0;
  if (readMT6701Counts(initialCounts)) {
    prevRawCounts = initialCounts;
    float rawDeg = (float)initialCounts * (360.0f / 16384.0f);
    sharedRawAngle = rawDeg;
    if (prefs.getBool("hasZero", false)) {
      float zeroOff = prefs.getFloat("zeroOff", 0.0f);
      float diffZero = rawDeg - zeroOff;
      while (diffZero > 180.0f) diffZero -= 360.0f;
      while (diffZero < -180.0f) diffZero += 360.0f;
      zeroContinuousAngle = rawDeg - diffZero;
    } else {
      zeroContinuousAngle = rawDeg;
    }
    sharedRealAngle = rawDeg - zeroContinuousAngle;
    prevRealAngle = sharedRealAngle;
    Serial.printf("Encoder detected. Initial raw: %.2f deg\n", rawDeg);
  } else {
    Serial.println("WARNING: MT6701 encoder not detected on I2C bus!");
  }

  // Configure Wi-Fi (AP + STA fallback)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("Access Point started: '%s' (PW: '%s')\n", AP_SSID, AP_PASS);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 && strlen(WIFI_SSID) > 0) {
    Serial.printf("Connecting to Wi-Fi '%s'...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 8000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connected! STA IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Wi-Fi STA connection timed out. Web UI accessible via AP.");
    }
  }

  // Web Server Routes
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/status", HTTP_GET, []() {
    char json[128];
    snprintf(json, sizeof(json),
             "{\"raw\":%.2f,\"real\":%.2f,\"pwm\":%d,\"err\":%s,\"target\":%.2f}",
             sharedRawAngle, sharedRealAngle, sharedPwmOut,
             i2cError ? "true" : "false", sharedTargetAngle);
    server.send(200, "application/json", json);
  });

  server.on("/config", HTTP_GET, []() {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f,\"minPwm\":%d,\"db\":%.2f,"
             "\"enP\":%s,\"enI\":%s,\"enD\":%s,\"motor\":%s,\"inv\":%s,\"target\":%.2f}",
             kp, ki, kd, minPwmOffset, deadbandDeg,
             enP ? "true" : "false", enI ? "true" : "false", enD ? "true" : "false",
             motorEnabled ? "true" : "false", invertMotor ? "true" : "false",
             sharedTargetAngle);
    server.send(200, "application/json", json);
  });

  server.on("/setTarget", HTTP_GET, []() {
    if (server.hasArg("val")) {
      sharedTargetAngle = server.arg("val").toFloat();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/setMotor", HTTP_GET, []() {
    if (server.hasArg("en")) {
      motorEnabled = (server.arg("en").toInt() == 1);
      if (!motorEnabled) {
        writePWM(0);
      }
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/setZero", HTTP_GET, []() {
    reqZeroCalib = true;
    server.send(200, "text/plain", "OK");
  });

  server.on("/setPid", HTTP_GET, []() {
    if (server.hasArg("kp")) kp = server.arg("kp").toFloat();
    if (server.hasArg("ki")) ki = server.arg("ki").toFloat();
    if (server.hasArg("kd")) kd = server.arg("kd").toFloat();
    if (server.hasArg("enP")) enP = (server.arg("enP").toInt() == 1);
    if (server.hasArg("enI")) enI = (server.arg("enI").toInt() == 1);
    if (server.hasArg("enD")) enD = (server.arg("enD").toInt() == 1);
    if (server.hasArg("minPwm")) minPwmOffset = server.arg("minPwm").toInt();
    if (server.hasArg("db")) deadbandDeg = server.arg("db").toFloat();
    if (server.hasArg("inv")) invertMotor = (server.arg("inv").toInt() == 1);

    prefs.putFloat("kp", kp);
    prefs.putFloat("ki", ki);
    prefs.putFloat("kd", kd);
    prefs.putBool("enP", enP);
    prefs.putBool("enI", enI);
    prefs.putBool("enD", enD);
    prefs.putInt("minPwm", minPwmOffset);
    prefs.putFloat("db", deadbandDeg);
    prefs.putBool("inv", invertMotor);

    server.send(200, "text/plain", "OK");
  });

  server.on("/testPolarity", []() {
    bool prevState = motorEnabled;
    motorEnabled = false; // Pause PID loop control
    delay(5);

    float startAngle = sharedRealAngle;
    writePWM(700);        // Gentle ~17% duty cycle nudge forward
    delay(80);            // Wait 80 milliseconds
    writePWM(0);          // Stop motor immediately
    delay(50);            // Let shaft settle
    float delta = sharedRealAngle - startAngle;

    String msg = "Start: " + String(startAngle, 2) + " deg, End: " + String(sharedRealAngle, 2) + " deg (Delta: " + String(delta, 2) + " deg).\n";
    if (fabs(delta) < 0.3f) {
      msg += "RESULT: Shaft barely moved. Increase test PWM slightly to overcome gearbox friction.";
    } else if (delta > 0) {
      msg += "RESULT: PASS! Motor polarity is CORRECT (Positive PWM increased angle).";
    } else {
      invertMotor = !invertMotor;
      prefs.putBool("inv", invertMotor);
      msg += "RESULT: INVERTED DETECTED! Automatically flipped 'invertMotor' and saved to Flash.";
    }
    (void)prevState;
    server.send(200, "text/plain", msg);
  });

  server.begin();
  Serial.println("Web server started.");

  // Launch PID Loop Task on Core 1
  xTaskCreatePinnedToCore(
      pidLoopTask,
      "pidLoopTask",
      4096,
      NULL,
      2,
      NULL,
      1
  );
  Serial.println("PID task started on Core 1.");
}

void loop() {
  server.handleClient();
  delay(2);
}