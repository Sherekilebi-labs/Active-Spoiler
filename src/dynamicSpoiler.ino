//#include <LSM303.h>

#include <Wire.h>
#include <ESP32Servo.h>
#include <FS.h>
#include <SPIFFS.h>
#include <math.h>
#include <ArduinoJson.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>

#include "web_ui.h"

// // Accelerometer (LSM303DLHC)
// #define ACC_ADDRESS 0x19
// #define CTRL_REG1_A 0x20
// #define OUT_X_L_A   0x28

Adafruit_MPU6050 mpu;

float roll, pitch;
float rollOffset = 0.0f;
float pitchOffset = 0.0f;
const char* CALIBRATION_FILE = "/calibration.json";
const char* AP_SSID = "SpoilerTuner";
const char* AP_PASSWORD = "CHANGE_ME_123";
WebServer server(80);

// Servo pins
Servo leftServo;
Servo rightServo;
const int leftServoPin = 26;
const int rightServoPin = 27;
const int servoMinUs = 500;
const int servoMaxUs = 2470;

// // Filtering
// float alpha = 0.5;
// float filteredRoll = 0.0;
// float filteredPitch = 0.0;
// const int N = 10;
// float rollBuffer[N] = {0};
// float pitchBuffer[N] = {0};
// int bufferIndex = 0;

// === USER-DEFINED MAPPING POINTS ===
#define MAX_PRESETS 3
#define POINTS 21

struct ServoMap {
  float rollInput[POINTS];
  float pitchInput[POINTS];
  float rollToRight[POINTS];
  float pitchToRight[POINTS];
  float rollToLeft[POINTS];
  float pitchToLeft[POINTS];
};
  
ServoMap presets[MAX_PRESETS];
int activePreset = 0;
int lastLeftServo = 0;
int lastRightServo = 0;
int actualLeftServo = -1;
int actualRightServo = -1;
bool leftAttached = false;
bool rightAttached = false;

// Fixed X-coordinates for all mapping curves (degrees) from -90 to +90 (21 points, 9° apart)
const float FIXED_INPUT_POINTS[POINTS] = {
  -90, -81, -72, -63, -54, -45, -36, -27, -18, -9,
   0,
   9, 18, 27, 36, 45, 54, 63, 72, 81, 90
};
inline float quantizeTenth(float v) { return roundf(v * 10.0f) / 10.0f; }
int findMirrorIndex(int idx) {
  if (idx < 0) return 0;
  if (idx >= POINTS) return POINTS - 1;
  return (POINTS - 1) - idx; // symmetrical grid
}
void enforcePitchTie(ServoMap& map) {
  for (int i = 0; i < POINTS; i++) {
    map.pitchToLeft[i] = map.pitchToRight[i];
  }
}
void enforceRollSymmetry(ServoMap& map) {
  for (int i = 0; i < POINTS; i++) {
    int m = findMirrorIndex(i);
    map.rollToLeft[i] = map.rollToRight[m];
  }
}

void setupWifi();
void setupWebServer();
void handleStatus();
void handlePresetGet();
void handleSetValue();
void handleSavePresetRequest();
void handleUsePresetRequest();
void handleCalibrateRequest();
void handleResetDefaults();
void handleNotFound();

template <size_t N>
void sendJsonResponse(StaticJsonDocument<N>& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(const char* message, int code = 400) {
  StaticJsonDocument<128> doc;
  doc["error"] = message;
  sendJsonResponse(doc, code);
}

void applyFixedInputs(ServoMap& map) {
  for (int i = 0; i < POINTS; i++) {
    map.rollInput[i] = FIXED_INPUT_POINTS[i];
    map.pitchInput[i] = FIXED_INPUT_POINTS[i];
  }
}

void fillPresetDefaults(int p) {
  applyFixedInputs(presets[p]);
  for (int i = 0; i < POINTS; i++) {
    float rollIn = presets[p].rollInput[i];
    float pitchIn = presets[p].pitchInput[i];

    float pitchOut = pitchIn > 0.0f ? pitchIn : 0.0f; // y = x for pitch > 0, else 0
    float rollOut = rollIn > 0.0f ? rollIn : 0.0f;    // y = x for roll > 0, else 0

    pitchOut = constrain(pitchOut, 0.0f, 100.0f);
    rollOut = constrain(rollOut, 0.0f, 100.0f);

    presets[p].pitchToRight[i] = pitchOut;
    presets[p].pitchToLeft[i]  = pitchOut;
    presets[p].rollToRight[i]  = rollOut;
    presets[p].rollToLeft[i]   = rollOut; // temporary; will be mirrored below
  }
  enforcePitchTie(presets[p]);
  enforceRollSymmetry(presets[p]);
}

void initializeDefaultPresets() {
  for (int p = 0; p < MAX_PRESETS; p++) {
    String path = "/preset_" + String(p) + ".json";
    if (!SPIFFS.exists(path)) {
      Serial.printf("Preset %d does not exist. Creating default...\n", p);
      fillPresetDefaults(p);
      savePreset(p);  // Save to SPIFFS
    }
  }
}

// === Interpolation ===
float interpolate(float xPoints[], float yPoints[], float x) {
  if (x <= xPoints[0]) return yPoints[0];
  if (x >= xPoints[POINTS - 1]) return yPoints[POINTS - 1];
  for (int i = 0; i < POINTS - 1; i++) {
    if (x >= xPoints[i] && x <= xPoints[i + 1]) {
      float ratio = (x - xPoints[i]) / (xPoints[i + 1] - xPoints[i]);
      return yPoints[i] + ratio * (yPoints[i + 1] - yPoints[i]);
    }
  }
  return 0; // Should not reach here due to bounds checks
}

bool saveCalibration() {
  File file = SPIFFS.open(CALIBRATION_FILE, FILE_WRITE);
  if (!file) return false;

  StaticJsonDocument<256> doc;
  doc["rollOffset"] = rollOffset;
  doc["pitchOffset"] = pitchOffset;

  bool ok = serializeJson(doc, file);
  file.close();
  return ok;
}

bool loadCalibration() {
  File file = SPIFFS.open(CALIBRATION_FILE, FILE_READ);
  if (!file) return false;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  rollOffset = doc["rollOffset"] | 0.0f;
  pitchOffset = doc["pitchOffset"] | 0.0f;
  return true;
}

int angleToMicroseconds(int angle) {
  int clamped = constrain(angle, 0, 180);
  return map(clamped, 0, 180, servoMinUs, servoMaxUs);
}

int microsecondsToAngle(int microseconds) {
  int clamped = constrain(microseconds, servoMinUs, servoMaxUs);
  float ratio = float(clamped - servoMinUs) / float(servoMaxUs - servoMinUs);
  return lroundf(ratio * 180.0f);
}

void setup() {
  Serial.begin(115200);
  
  while (!Serial);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) delay(10);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  // Wait for sensor to stabilize
  delay(1000);
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  // Sensor rotated: swap roll/pitch formulas
  roll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180 / PI;
  pitch = atan2(a.acceleration.y, a.acceleration.z) * 180 / PI;
  (void)g;
  (void)t;

  leftServo.setPeriodHertz(50);
  rightServo.setPeriodHertz(50);
  leftServo.attach(leftServoPin, servoMinUs, servoMaxUs);
  rightServo.attach(rightServoPin, servoMinUs, servoMaxUs);

  SPIFFS.begin(true); // Automatically formats if corrupt or uninitialized

  if (loadCalibration()) {
    Serial.printf("Calibration loaded. Roll offset: %.2f, Pitch offset: %.2f\n", rollOffset, pitchOffset);
  } else {
    Serial.println("Calibration not found. Using defaults.");
    saveCalibration();
  }

  initializeDefaultPresets(); // <- this initializes and saves all 3 presets

  for (int i = 0; i < MAX_PRESETS; i++) {
    if (loadPreset(i)) {
      Serial.printf("Preset %d loaded successfully.\n", i);
    } else {
      Serial.printf("Failed to load preset %d.\n", i);
    }
  }

  setupWifi();
  setupWebServer();
}

void loop() {
  
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  // Raw angles from accelerometer
  // Sensor rotated: swap roll/pitch formulas
  float accRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180 / PI;
  float accPitch = atan2(a.acceleration.y, a.acceleration.z) * 180 / PI;

  // Use raw accelerometer angles
  roll = accRoll;
  pitch = accPitch;
  (void)g;
  (void)t;

  float calibratedRoll = roll - rollOffset;
  float calibratedPitch = pitch - pitchOffset;

  // Optional noise threshold
  static float prevRoll = 0, prevPitch = 0;
  const float errorThreshold = 0.1;


  if (abs(calibratedRoll - prevRoll) > errorThreshold || abs(calibratedPitch - prevPitch) > errorThreshold) {
    Serial.print("Roll: ");
    Serial.print(calibratedRoll);
    Serial.print("°, Pitch: ");
    Serial.println(calibratedPitch);
    prevRoll = calibratedRoll;
    prevPitch = calibratedPitch;
  }

  // === Apply Mapping Functions ===
  // Interpolate using current preset
  ServoMap &map = presets[activePreset];

  int rightServoAngle = 0;
  int leftServoAngle  = 0;

  // Simple linear interpolation functions you’ll need to define:
  rightServoAngle += interpolate(map.rollInput, map.rollToRight, calibratedRoll);
  rightServoAngle += interpolate(map.pitchInput, map.pitchToRight, calibratedPitch);
  leftServoAngle  += interpolate(map.rollInput, map.rollToLeft, calibratedRoll);
  leftServoAngle  += interpolate(map.pitchInput, map.pitchToLeft, calibratedPitch);

  rightServoAngle = constrain(rightServoAngle, 0, 80);
  leftServoAngle = constrain(leftServoAngle, 0, 80);

  lastRightServo = rightServoAngle;
  lastLeftServo = leftServoAngle;
  int rightServoPulse = angleToMicroseconds(180 - rightServoAngle);
  int leftServoPulse = angleToMicroseconds(leftServoAngle);
  rightServo.writeMicroseconds(rightServoPulse);
  leftServo.writeMicroseconds(leftServoPulse);
  leftAttached = leftServo.attached();
  rightAttached = rightServo.attached();
  if (leftAttached) {
    actualLeftServo = constrain(microsecondsToAngle(leftServoPulse), 0, 180);
  } else {
    actualLeftServo = -1;
  }
  if (rightAttached) {
    int rightPhysicalAngle = microsecondsToAngle(rightServoPulse);
    actualRightServo = constrain(180 - rightPhysicalAngle, 0, 180);
  } else {
    actualRightServo = -1;
  }

  // Serial.print(" servo angle: ");
  // Serial.println(leftServoAngle);

  handleSerialCommands();

  server.handleClient();
  delay(10);
}


bool savePreset(int preset) {
  if (preset < 0 || preset >= MAX_PRESETS) return false;

  String filename = "/preset_" + String(preset) + ".json";
  File file = SPIFFS.open(filename, FILE_WRITE);
  if (!file) return false;

  StaticJsonDocument<4096> doc;

  // Ensure X-points remain fixed when persisting
  applyFixedInputs(presets[preset]);
  enforcePitchTie(presets[preset]);
  enforceRollSymmetry(presets[preset]);

  JsonArray rIn  = doc.createNestedArray("rollInput");
  JsonArray pIn  = doc.createNestedArray("pitchInput");
  JsonArray r2R  = doc.createNestedArray("rollToRight");
  JsonArray p2R  = doc.createNestedArray("pitchToRight");
  JsonArray r2L  = doc.createNestedArray("rollToLeft");
  JsonArray p2L  = doc.createNestedArray("pitchToLeft");

  for (int i = 0; i < POINTS; i++) {
    rIn.add(presets[preset].rollInput[i]);
    pIn.add(presets[preset].pitchInput[i]);
    r2R.add(serialized(String(quantizeTenth(presets[preset].rollToRight[i]), 1)));
    p2R.add(serialized(String(quantizeTenth(presets[preset].pitchToRight[i]), 1)));
    r2L.add(serialized(String(quantizeTenth(presets[preset].rollToLeft[i]), 1)));
    p2L.add(serialized(String(quantizeTenth(presets[preset].pitchToLeft[i]), 1)));
  }

  bool ok = serializeJson(doc, file);
  file.close();
  return ok;
}

bool loadPreset(int preset) {
  if (preset < 0 || preset >= MAX_PRESETS) return false;

  String filename = "/preset_" + String(preset) + ".json";
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) return false;

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  bool sizeMismatch = false;
  if (doc["rollInput"].size() != POINTS || doc["pitchInput"].size() != POINTS ||
      doc["rollToRight"].size() != POINTS || doc["pitchToRight"].size() != POINTS ||
      doc["rollToLeft"].size() != POINTS || doc["pitchToLeft"].size() != POINTS) {
    sizeMismatch = true;
  }

  if (sizeMismatch) {
    fillPresetDefaults(preset);
    savePreset(preset);
    return true;
  }

  for (int i = 0; i < POINTS; i++) {
    presets[preset].rollInput[i]     = doc["rollInput"][i]     | 0.0f;
    presets[preset].pitchInput[i]    = doc["pitchInput"][i]    | 0.0f;
    presets[preset].rollToRight[i]   = quantizeTenth(doc["rollToRight"][i].as<float>());
    presets[preset].pitchToRight[i]  = quantizeTenth(doc["pitchToRight"][i].as<float>());
    presets[preset].rollToLeft[i]    = quantizeTenth(doc["rollToLeft"][i].as<float>());
    presets[preset].pitchToLeft[i]   = quantizeTenth(doc["pitchToLeft"][i].as<float>());
  }

  // Enforce fixed X-coordinates
  applyFixedInputs(presets[preset]);
  enforcePitchTie(presets[preset]);
  enforceRollSymmetry(presets[preset]);

  return true;
}

void setupWifi() {
  WiFi.mode(WIFI_AP);
  bool started = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (started) {
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("WiFi AP started. SSID: %s, IP: %s\n", AP_SSID, ip.toString().c_str());
  } else {
    Serial.println("Failed to start WiFi AP.");
  }
}

void handleStatus() {
  StaticJsonDocument<256> doc;
  doc["roll"] = roll;
  doc["pitch"] = pitch;
  doc["calibratedRoll"] = roll - rollOffset;
  doc["calibratedPitch"] = pitch - pitchOffset;
  doc["rollOffset"] = rollOffset;
  doc["pitchOffset"] = pitchOffset;
  doc["activePreset"] = activePreset;
  doc["rightServo"] = lastRightServo;
  doc["leftServo"] = lastLeftServo;
  doc["rightConnected"] = rightAttached;
  doc["leftConnected"] = leftAttached;
  doc["rightActual"] = actualRightServo;
  doc["leftActual"] = actualLeftServo;
  doc["rightDelta"] = rightAttached ? (float)(actualRightServo - lastRightServo) : 0.0f;
  doc["leftDelta"] = leftAttached ? (float)(actualLeftServo - lastLeftServo) : 0.0f;
  sendJsonResponse(doc);
}

void handlePresetGet() {
  if (!server.hasArg("p")) {
    sendError("Missing preset parameter");
    return;
  }

  int preset = server.arg("p").toInt();
  if (preset < 0 || preset >= MAX_PRESETS) {
    sendError("Preset out of range");
    return;
  }

  enforcePitchTie(presets[preset]);
  enforceRollSymmetry(presets[preset]);

  StaticJsonDocument<4096> doc;
  JsonArray rIn  = doc.createNestedArray("rollInput");
  JsonArray pIn  = doc.createNestedArray("pitchInput");
  JsonArray r2R  = doc.createNestedArray("rollToRight");
  JsonArray p2R  = doc.createNestedArray("pitchToRight");
  JsonArray r2L  = doc.createNestedArray("rollToLeft");
  JsonArray p2L  = doc.createNestedArray("pitchToLeft");

  for (int i = 0; i < POINTS; i++) {
    rIn.add(presets[preset].rollInput[i]);
    pIn.add(presets[preset].pitchInput[i]);
    r2R.add(serialized(String(quantizeTenth(presets[preset].rollToRight[i]), 1)));
    p2R.add(serialized(String(quantizeTenth(presets[preset].pitchToRight[i]), 1)));
    r2L.add(serialized(String(quantizeTenth(presets[preset].rollToLeft[i]), 1)));
    p2L.add(serialized(String(quantizeTenth(presets[preset].pitchToLeft[i]), 1)));
  }

  doc["preset"] = preset;
  sendJsonResponse(doc);
}

void handleSetValue() {
  String body = server.arg("plain");
  if (body.length() == 0) {
    sendError("Empty body");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendError("Invalid JSON");
    return;
  }

  int preset = doc["preset"] | -1;
  const char* field = doc["field"] | "";
  int index = doc["index"] | -1;
  float value = doc["value"] | 0.0f;

  if (preset < 0 || preset >= MAX_PRESETS || index < 0 || index >= POINTS) {
    sendError("Preset or index out of range");
    return;
  }

  float clamped = constrain(value, 0.0f, 100.0f);
  float quantized = quantizeTenth(clamped);
  ServoMap& map = presets[preset];
  if      (strcmp(field, "rollInput") == 0 || strcmp(field, "pitchInput") == 0) {
    sendError("Input points are fixed");
    return;
  }
  else if (strcmp(field, "rollToRight") == 0) {
    map.rollToRight[index] = quantized;
    enforceRollSymmetry(map);
  }
  else if (strcmp(field, "pitchToRight") == 0) {
    map.pitchToRight[index] = quantized;
    enforcePitchTie(map);
  }
  else if (strcmp(field, "rollToLeft") == 0) {
    int mirror = findMirrorIndex(index);
    map.rollToRight[mirror] = quantized;
    enforceRollSymmetry(map);
  }
  else if (strcmp(field, "pitchToLeft") == 0) {
    map.pitchToRight[index] = quantized;
    enforcePitchTie(map);
  }
  else {
    sendError("Unknown field");
    return;
  }

  // Persist immediately so graph edits survive restart
  bool saved = savePreset(preset);

  StaticJsonDocument<64> res;
  res["ok"] = saved;
  res["saved"] = saved;
  res["value"] = quantized;
  sendJsonResponse(res, saved ? 200 : 500);
}

void handleSavePresetRequest() {
  String body = server.arg("plain");
  StaticJsonDocument<128> doc;
  if (body.length()) {
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      sendError("Invalid JSON");
      return;
    }
  }

  int preset = doc["preset"] | activePreset;
  if (preset < 0 || preset >= MAX_PRESETS) {
    sendError("Preset out of range");
    return;
  }

  bool ok = savePreset(preset);
  StaticJsonDocument<64> res;
  res["ok"] = ok;
  sendJsonResponse(res, ok ? 200 : 500);
}

void handleUsePresetRequest() {
  String body = server.arg("plain");
  StaticJsonDocument<128> doc;
  if (body.length()) {
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      sendError("Invalid JSON");
      return;
    }
  }

  int preset = doc["preset"] | 0;
  if (preset < 0 || preset >= MAX_PRESETS) {
    sendError("Preset out of range");
    return;
  }

  activePreset = preset;
  StaticJsonDocument<64> res;
  res["ok"] = true;
  res["activePreset"] = activePreset;
  sendJsonResponse(res);
}

void handleCalibrateRequest() {
  rollOffset = roll;
  pitchOffset = pitch;
  bool ok = saveCalibration();

  StaticJsonDocument<128> res;
  res["ok"] = ok;
  res["rollOffset"] = rollOffset;
  res["pitchOffset"] = pitchOffset;
  sendJsonResponse(res, ok ? 200 : 500);
}

void handleResetDefaults() {
  String body = server.arg("plain");
  StaticJsonDocument<64> doc;
  if (body.length()) {
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      sendError("Invalid JSON");
      return;
    }
  }
  int preset = doc["preset"] | activePreset;
  if (preset < 0 || preset >= MAX_PRESETS) {
    sendError("Preset out of range");
    return;
  }
  fillPresetDefaults(preset);
  bool ok = savePreset(preset);
  StaticJsonDocument<64> res;
  res["ok"] = ok;
  res["preset"] = preset;
  sendJsonResponse(res, ok ? 200 : 500);
}

void handleNotFound() {
  sendError("Not found", 404);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/preset", HTTP_GET, handlePresetGet);
  server.on("/set", HTTP_POST, handleSetValue);
  server.on("/save", HTTP_POST, handleSavePresetRequest);
  server.on("/use", HTTP_POST, handleUsePresetRequest);
  server.on("/calibrate", HTTP_POST, handleCalibrateRequest);
  server.on("/reset", HTTP_POST, handleResetDefaults);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");
}

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("set")) {
      int preset; char field[20]; int index; float value;
      int count = sscanf(command.c_str(), "set %d %s %d %f", &preset, field, &index, &value);
      if (count == 4 && preset >= 0 && preset < MAX_PRESETS && index >= 0 && index < POINTS) {
        ServoMap& map = presets[preset];
        if      (strcmp(field, "rollInput") == 0 || strcmp(field, "pitchInput") == 0) {
          Serial.println("Input points are fixed and cannot be changed.");
          return;
        }
        float clamped = constrain(value, 0.0f, 100.0f);
        float quantized = quantizeTenth(clamped);
        if      (strcmp(field, "rollToRight") == 0)    map.rollToRight[index] = quantized;
        else if (strcmp(field, "pitchToRight") == 0)   map.pitchToRight[index] = quantized;
        else if (strcmp(field, "rollToLeft") == 0) {
          int mirror = findMirrorIndex(index);
          map.rollToRight[mirror] = quantized;
        }
        else if (strcmp(field, "pitchToLeft") == 0)    map.pitchToRight[index] = quantized;
        else {
          Serial.println("Unknown field.");
          return;
        }
        enforcePitchTie(map);
        enforceRollSymmetry(map);
        if (savePreset(preset)) {
          Serial.println("Value set and saved.");
        } else {
          Serial.println("Value set but failed to save.");
        }
      } else {
        Serial.println("Usage: set <preset> <field> <index> <value>");
      }
    }

    else if (command.startsWith("show")) {
      int preset;
      if (sscanf(command.c_str(), "show %d", &preset) == 1 && preset >= 0 && preset < MAX_PRESETS) {
        ServoMap& map = presets[preset];
        Serial.printf("Preset %d:\n", preset);
        for (int i = 0; i < POINTS; i++) {
          Serial.printf("[%2d] rollInput: %.2f, pitchInput: %.2f, rollToRight: %.2f, pitchToRight: %.2f, rollToLeft: %.2f, pitchToLeft: %.2f\n",
            i, map.rollInput[i], map.pitchInput[i],
            map.rollToRight[i], map.pitchToRight[i],
            map.rollToLeft[i], map.pitchToLeft[i]);
        }
      } else {
        Serial.println("Usage: show <preset>");
      }
    }

    else if (command.startsWith("save")) {
      int preset;
      if (sscanf(command.c_str(), "save %d", &preset) == 1 && preset >= 0 && preset < MAX_PRESETS) {
        if (savePreset(preset)) {
          Serial.println("Preset saved.");
        } else {
          Serial.println("Failed to save preset.");
        }
      } else {
        Serial.println("Usage: save <preset>");
      }
    }

    else if (command == "callibrate" || command == "calibrate") {
      rollOffset = roll;
      pitchOffset = pitch;
      if (saveCalibration()) {
        Serial.printf("Calibration saved. New offsets -> Roll: %.2f, Pitch: %.2f\n", rollOffset, pitchOffset);
      } else {
        Serial.println("Failed to save calibration.");
      }
    }

    else if (command.startsWith("use")) {
    int p;
    if (sscanf(command.c_str(), "use %d", &p) == 1 && p >= 0 && p < MAX_PRESETS) {
      activePreset = p;
      Serial.printf("Using preset %d\n", p);
    } else {
      Serial.println("Usage: use <preset>");
      }
    }
    else if (command == "help") {
    Serial.println("Commands:");
    Serial.println("  set <preset> <field> <index> <value>");
    Serial.println("  show <preset>");
    Serial.println("  save <preset>");
    Serial.println("  use <preset>");
    Serial.println("  callibrate");
    }

    else {
      Serial.println("Unknown command. Use: set, show, save, use, callibrate");
    }
  }
}
