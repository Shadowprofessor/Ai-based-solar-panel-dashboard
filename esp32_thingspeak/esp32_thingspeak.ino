// ════════════════════════════════════════════════════════════════════════
//  IoT Smart Automatic Light Control — ESP32 BRIDGE
//  File: esp32_thingspeak/esp32_thingspeak.ino
//
//  Converted from ESP8266 to ESP32.
//  Key changes vs the ESP8266 version:
//    • WiFi.h / HTTPClient.h  replace  ESP8266WiFi.h / ESP8266HTTPClient.h
//    • SoftwareSerial replaced by ESP32 Hardware Serial2 (more reliable)
//    • Serial2 default pins: RX = GPIO16, TX = GPIO17
//    • WiFiClient passed into HTTPClient via http.begin(client, url)
//    • Serial.printf() works identically on ESP32
//    • F() macro kept for readability (less critical on ESP32 but harmless)
//
//  ROLE
//    Bridge between the Arduino (real sensor + relay) and ThingSpeak.
//        ThingSpeak  <->  ESP32  <->  Arduino
//
//  WIRING (ESP32 DevKit)
//    GPIO16 (Serial2 RX) ← Arduino D11 TX  [via 10K/20K voltage divider]
//    GPIO17 (Serial2 TX) → Arduino D10 RX  [direct — ESP32 TX is 3.3V]
//    GND ──────────────── Arduino GND       [COMMON GND REQUIRED]
//
//  NOTE ON VOLTAGE DIVIDER
//    Arduino D11 TX outputs 5V logic. ESP32 GPIO is 3.3V tolerant only.
//    Use a 10K / 20K resistor divider on that single wire:
//      Arduino D11 ─── 10K ─── GPIO16 ─── 20K ─── GND
//    ESP32 TX (GPIO17, 3.3V) → Arduino D10 RX is safe direct.
//
//  ════════════════════════════════════════════════════════════════════
//   THINGSPEAK CHANNEL — FOUR FIELDS
//  ════════════════════════════════════════════════════════════════════
//    field1 = LDR      1 = DARK,  0 = BRIGHT
//    field2 = Relay    1 = ON,    0 = OFF
//    field3 = LED      1 = ON,    0 = OFF
//    field4 = Mode     1 = AUTO, 2 = MANUAL ON, 3 = MANUAL OFF
//
//  ════════════════════════════════════════════════════════════════════
//   CYCLE ORDER (never uploads stale hardware state)
//  ════════════════════════════════════════════════════════════════════
//    read feed / decide mode  ->  command the Arduino  ->  settle  ->
//    request STATUS  ->  receive the ACTUAL state  ->  upload F1..F4
// ════════════════════════════════════════════════════════════════════════

// ── ESP32 headers (replaces ESP8266WiFi + ESP8266HTTPClient) ───────────
#include <WiFi.h>           // ← ESP32
#include <HTTPClient.h>     // ← ESP32
#include <WiFiClient.h>

// No SoftwareSerial needed — ESP32 has 3 hardware UARTs.
// Serial2 uses GPIO16 (RX) and GPIO17 (TX) by default.

#include "secrets.h"        // WIFI_SSID, WIFI_PASS, TS_WRITE_KEY,
                            // TS_READ_KEY, TS_CHANNEL

const char* TS_HOST = "api.thingspeak.com";

// ── Hardware Serial2 to the Arduino ────────────────────────────────────
// Serial2 is a HardwareSerial instance built into the ESP32 core.
// Default pins: RX = GPIO16, TX = GPIO17. Change here if you rewire.
#define ARDU_RX 16
#define ARDU_TX 17

// ── Timing ─────────────────────────────────────────────────────────────
const unsigned long CYCLE_MS          = 20000UL;
const unsigned long MIN_UPLOAD_GAP_MS = 16000UL;
const unsigned long HEARTBEAT_MS      = 60000UL;
const unsigned long MANUAL_TIMEOUT_MS = 1800000UL; // 30 min; 0 = never
const unsigned long SETTLE_MS         = 400UL;

unsigned long lastCycleAt  = 0;
unsigned long lastUploadAt = 0;
unsigned long manualSetAt  = 0;

// ── Control mode ───────────────────────────────────────────────────────
enum Mode { MODE_AUTO, MODE_MANUAL_ON, MODE_MANUAL_OFF };
Mode currentMode = MODE_AUTO;

const int MODE_CODE_AUTO = 1;
const int MODE_CODE_ON   = 2;
const int MODE_CODE_OFF  = 3;

Mode modeFromCode(int c) {
  if (c == MODE_CODE_ON)  return MODE_MANUAL_ON;
  if (c == MODE_CODE_OFF) return MODE_MANUAL_OFF;
  return MODE_AUTO;
}
int modeCode(Mode m) {
  if (m == MODE_MANUAL_ON)  return MODE_CODE_ON;
  if (m == MODE_MANUAL_OFF) return MODE_CODE_OFF;
  return MODE_CODE_AUTO;
}

// ── Actual hardware state ──────────────────────────────────────────────
int  ldrStatus   = 0;
int  relayStatus = 0;
int  ledStatus   = 0;
bool haveStatus  = false;

String pushBuf = "";

// ── Last uploaded values (change detection) ────────────────────────────
int upLdr = -1, upRelay = -1, upLed = -1, upMode = -1;

// ── Command bookkeeping ────────────────────────────────────────────────
long lastProcessedId = -1;

// ════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[ESP32] ================================"));
  Serial.println(F("[ESP32]  Smart Automatic Light — Bridge"));
  Serial.println(F("[ESP32]  Fields: F1=LDR F2=Relay F3=LED"));
  Serial.println(F("[ESP32]  Field4: 1=AUTO 2=ON 3=OFF"));
  Serial.println(F("[ESP32]  HW Serial2: RX=GPIO16, TX=GPIO17"));
  Serial.println(F("[ESP32] ================================"));

  // ── Start Serial2 on the chosen GPIO pins ────────────────────────────
  Serial2.begin(9600, SERIAL_8N1, ARDU_RX, ARDU_TX);

  connectWiFi();

  currentMode = MODE_AUTO;
  sendCommandToArduino("AUTO");
  delay(SETTLE_MS);
  readStatusFromArduino();

  lastCycleAt = millis() - CYCLE_MS + 2000UL;
}

// ════════════════════════════════════════════════════════════════════════
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WIFI] Lost connection, reconnecting…"));
    connectWiFi();
  }

  // Event path: Arduino pushed a change unsolicited
  if (drainArduinoPush()) {
    Serial.println(F("[EVENT] Arduino pushed a state change"));
    maybeUpload();
  }

  // Periodic path: poll ThingSpeak, heartbeat
  if (millis() - lastCycleAt >= CYCLE_MS) {
    lastCycleAt = millis();
    runCycle();
  }

  expireManualOverride();
}

// ════════════════════════════════════════════════════════════════════════
//  MAIN CYCLE
// ════════════════════════════════════════════════════════════════════════
void runCycle() {
  Serial.println(F("\n──── cycle ─────────────────────────"));

  Mode before = currentMode;
  pollForWebsiteCommand();

  sendCommandToArduino(modeToCommand(currentMode));

  if (currentMode != before) {
    Serial.print(F("[MODE] changed to "));
    Serial.println(modeName(currentMode));
  }

  delay(SETTLE_MS);

  if (!readStatusFromArduino()) {
    Serial.println(F("[ARDU] No valid response — nothing uploaded"));
    Serial.println(F("────────────────────────────────────"));
    return;
  }

  maybeUpload();
  Serial.println(F("────────────────────────────────────"));
}

// ════════════════════════════════════════════════════════════════════════
//  ARDUINO LINK  — uses Serial2 instead of SoftwareSerial
// ════════════════════════════════════════════════════════════════════════

void sendCommandToArduino(const char* cmd) {
  Serial2.println(cmd);          // ← Serial2, not ArduSerial
  Serial.print(F("[ESP->ARD] "));
  Serial.println(cmd);
}

const char* modeToCommand(Mode m) {
  switch (m) {
    case MODE_MANUAL_ON:  return "ON";
    case MODE_MANUAL_OFF: return "OFF";
    default:              return "AUTO";
  }
}

const char* modeName(Mode m) {
  switch (m) {
    case MODE_MANUAL_ON:  return "MANUAL ON";
    case MODE_MANUAL_OFF: return "MANUAL OFF";
    default:              return "AUTO";
  }
}

bool readStatusFromArduino() {
  while (Serial2.available()) Serial2.read();   // flush stale bytes
  pushBuf = "";

  sendCommandToArduino("STATUS");

  unsigned long t0 = millis();
  String line = "";

  while (millis() - t0 < 1500UL) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') {
        line.trim();
        if (line.indexOf("LDR:") >= 0) {
          applyStatusLine(line);
          return true;
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    delay(2);
  }
  return false;
}

bool drainArduinoPush() {
  bool changed = false;

  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      pushBuf.trim();
      if (pushBuf.indexOf("LDR:") >= 0) {
        int pl = ldrStatus, pr = relayStatus, pe = ledStatus;
        applyStatusLine(pushBuf);
        if (pl != ldrStatus || pr != relayStatus || pe != ledStatus)
          changed = true;
      }
      pushBuf = "";
    } else if (c != '\r') {
      pushBuf += c;
      if (pushBuf.length() > 48) pushBuf = "";
    }
  }
  return changed;
}

void applyStatusLine(const String& line) {
  ldrStatus   = extractInt(line, "LDR:");
  relayStatus = extractInt(line, "RELAY:");
  ledStatus   = extractInt(line, "LED:");
  haveStatus  = true;

  Serial.printf("[ARD->ESP] LDR=%d RELAY=%d LED=%d  (mode=%s)\n",
                ldrStatus, relayStatus, ledStatus, modeName(currentMode));
}

int extractInt(const String& s, const char* key) {
  int idx = s.indexOf(key);
  if (idx < 0) return 0;
  idx += strlen(key);
  String num = "";
  while (idx < (int)s.length() && (isDigit(s[idx]) || s[idx] == '-'))
    num += s[idx++];
  return num.toInt();
}

// ════════════════════════════════════════════════════════════════════════
//  UPLOAD
// ════════════════════════════════════════════════════════════════════════
void maybeUpload() {
  if (!haveStatus) return;
  if (WiFi.status() != WL_CONNECTED) return;

  bool changed   = (ldrStatus   != upLdr)   ||
                   (relayStatus != upRelay)  ||
                   (ledStatus   != upLed)    ||
                   (modeCode(currentMode) != upMode);
  bool heartbeat = (millis() - lastUploadAt >= HEARTBEAT_MS);

  if (!changed && !heartbeat) return;

  if (lastUploadAt != 0 && millis() - lastUploadAt < MIN_UPLOAD_GAP_MS) {
    Serial.println(F("[TS] Throttled (15 s channel limit) — will retry"));
    return;
  }

  uploadToThingSpeak(changed ? "change" : "heartbeat");
}

void uploadToThingSpeak(const char* reason) {
  WiFiClient client;
  HTTPClient http;

  int mode = modeCode(currentMode);

  String url = String("http://") + TS_HOST + "/update"
             + "?api_key=" + TS_WRITE_KEY
             + "&field1="  + ldrStatus
             + "&field2="  + relayStatus
             + "&field3="  + ledStatus
             + "&field4="  + mode;

  http.begin(client, url);          // same API as ESP8266 HTTPClient
  http.setTimeout(6000);
  int    code = http.GET();
  String body = http.getString();
  http.end();

  body.trim();
  long entryId = body.toInt();

  Serial.printf("[TS] Upload (%s) F1=%d F2=%d F3=%d F4=%d → ",
                reason, ldrStatus, relayStatus, ledStatus, mode);

  if (code == 200 && entryId > 0) {
    lastUploadAt = millis();
    upLdr   = ldrStatus;
    upRelay = relayStatus;
    upLed   = ledStatus;
    upMode  = mode;
    Serial.printf("OK (entry %ld)\n", entryId);
  } else {
    Serial.printf("FAILED (HTTP %d, body='%s') — will retry\n",
                  code, body.c_str());
  }
}

// ════════════════════════════════════════════════════════════════════════
//  COMMAND POLL
// ════════════════════════════════════════════════════════════════════════
void pollForWebsiteCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + TS_HOST
             + "/channels/" + TS_CHANNEL + "/feeds.json"
             + "?api_key=" + TS_READ_KEY + "&results=10";

  http.begin(client, url);
  http.setTimeout(6000);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[TS] Feed read failed: HTTP %d — keeping mode %s\n",
                  code, modeName(currentMode));
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  long bestId = -1;
  int  bestF4 = -1;
  int  scan   = 0;

  while (true) {
    int at = body.indexOf("\"entry_id\":", scan);
    if (at < 0) break;

    int next    = body.indexOf("\"entry_id\":", at + 11);
    String chunk = (next < 0) ? body.substring(at)
                              : body.substring(at, next);
    scan = at + 11;

    long id = jsonNumber(chunk, "\"entry_id\":");
    if (id <= 0 || id <= lastProcessedId) continue;

    int f2 = jsonField(chunk, "\"field2\":");
    if (f2 >= 0) continue;             // ESP status, not a command

    int f4 = jsonField(chunk, "\"field4\":");
    if (f4 != MODE_CODE_AUTO && f4 != MODE_CODE_ON && f4 != MODE_CODE_OFF)
      continue;
    if (id <= bestId) continue;

    bestId = id;
    bestF4 = f4;
  }

  if (bestId < 0) {
    Serial.printf("[TS] No new website command — mode stays %s\n",
                  modeName(currentMode));
    return;
  }

  lastProcessedId = bestId;
  currentMode     = modeFromCode(bestF4);
  manualSetAt     = (currentMode == MODE_AUTO) ? 0 : millis();

  Serial.printf("[TS] Command entry %ld → %s\n",
                bestId, modeName(currentMode));
}

long jsonNumber(const String& s, const char* key) {
  int idx = s.indexOf(key);
  if (idx < 0) return -1;
  idx += strlen(key);
  while (idx < (int)s.length() && (s[idx] == ' ' || s[idx] == '"')) idx++;
  String num = "";
  while (idx < (int)s.length() && (isDigit(s[idx]) || s[idx] == '-'))
    num += s[idx++];
  return num.length() ? num.toInt() : -1;
}

int jsonField(const String& s, const char* key) {
  int idx = s.indexOf(key);
  if (idx < 0) return -1;
  idx += strlen(key);
  while (idx < (int)s.length() && s[idx] == ' ') idx++;
  if (s.startsWith("null", idx)) return -1;
  if (idx < (int)s.length() && s[idx] == '"') idx++;
  String num = "";
  while (idx < (int)s.length() && (isDigit(s[idx]) || s[idx] == '-'))
    num += s[idx++];
  return num.length() ? num.toInt() : -1;
}

// ════════════════════════════════════════════════════════════════════════
//  MANUAL OVERRIDE EXPIRY
// ════════════════════════════════════════════════════════════════════════
void expireManualOverride() {
  if (MANUAL_TIMEOUT_MS == 0)   return;
  if (currentMode == MODE_AUTO) return;
  if (manualSetAt == 0)         return;
  if (millis() - manualSetAt < MANUAL_TIMEOUT_MS) return;

  Serial.println(F("[MODE] Manual override expired → AUTO"));
  currentMode = MODE_AUTO;
  manualSetAt = 0;
  sendCommandToArduino("AUTO");

  delay(SETTLE_MS);
  readStatusFromArduino();
  maybeUpload();
}

// ════════════════════════════════════════════════════════════════════════
//  WIFI
// ════════════════════════════════════════════════════════════════════════
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print(F("[WIFI] Connecting"));
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print('.');
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] Connected — IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WIFI] Failed — will retry on next loop"));
  }
}
