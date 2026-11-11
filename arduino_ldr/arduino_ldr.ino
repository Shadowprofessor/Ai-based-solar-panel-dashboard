// ════════════════════════════════════════════════════════════════════════
//  IoT Smart Automatic Light Control — ARDUINO SIDE
//  File: arduino_ldr/arduino_ldr.ino
//
//  ROLE
//    The Arduino owns the REAL automation. It reads the LDR, decides
//    dark/bright, and drives the relay + LED. It is fully event-actuated:
//    the relay is only written when the decision actually FLIPS, never on
//    a timer, and every change is pushed to the ESP8266 immediately.
//
//  WIRING
//    LDR        → A0        (analog)
//    Relay IN   → D7        (ACTIVE LOW : LOW = ON, HIGH = OFF)  ✅ FIXED
//    LED        → D13       (ACTIVE HIGH : HIGH = ON, LOW  = OFF)
//    ESP TX     → D10       (Arduino SoftSerial RX)   [3.3 V direct, OK]
//    ESP RX     ← D11       (Arduino SoftSerial TX)   [via 10K/20K divider]
//    GND ─────────────────────── ESP GND              [COMMON GND REQUIRED]
//
//  THRESHOLD LOGIC          (THRESHOLD = 620, HYSTERESIS = 4)
//    analogRead(A0) >= 620       →  DARK    →  Relay ON,  LED ON
//    analogRead(A0) 616 … 619    →  hold the current decision (see below)
//    analogRead(A0) <= 615       →  BRIGHT  →  Relay OFF, LED OFF
//
//    The reading RISES as the room gets darker, which is what you get when
//    the LDR sits in the upper leg of the divider (5V — LDR — A0 — R — GND).
//    If you ever rewire the divider the other way round, flip the two
//    comparisons in sampleLdr(), setup() and enterAuto().
//
//  SERIAL COMMANDS FROM ESP8266 (9600 baud, newline terminated)
//    STATUS  →  reply  LDR:x,RELAY:x,LED:x
//    ON      →  MANUAL ON  : relay ON,  LED ON   (LDR ignored)
//    OFF     →  MANUAL OFF : relay OFF, LED OFF  (LDR ignored)
//    AUTO    →  resume LDR control, re-evaluate the LDR immediately
//
//  STATUS FORMAT (also pushed unsolicited on every change)
//    LDR:x,RELAY:x,LED:x       LDR   1 = DARK,  0 = BRIGHT
//                              RELAY 1 = ON,    0 = OFF
//                              LED   1 = ON,    0 = OFF
//
//  ─── MODE IS HELD IN RAM ONLY ──────────────────────────────────────────
//  This sketch never sends its mode over the serial link and never touches
//  ThingSpeak. "AUTO / MANUAL ON / MANUAL OFF" exists only as the enum in
//  RAM here and as the AUTO / ON / OFF words on the wire. The channel's
//  field4 = Mode is written by the ESP and the dashboard, never here.
// ════════════════════════════════════════════════════════════════════════

#include <SoftwareSerial.h>

// ── Pin definitions ────────────────────────────────────────────────────
#define LDR_PIN   A0
#define RELAY_PIN 7
#define LED_PIN   13

// ✅ FIXED: true = ACTIVE LOW (the standard cheap blue relay module).
// LOW  on D7 → relay coil energised → contact closes → light ON
// HIGH on D7 → relay coil de-energised → contact open → light OFF
// applyOutputs() and sendStatus() both derive from this flag so the
// drive signal and the status report can never disagree.
const bool RELAY_ACTIVE_LOW = true;   // ← was false, now true

// ── SoftwareSerial to the ESP8266 ──────────────────────────────────────
//   D10 = RX  ← ESP D6 (GPIO12)   direct
//   D11 = TX  → ESP D5 (GPIO14)   through a 10 kΩ / 20 kΩ divider (3.3 V)
SoftwareSerial espSerial(10, 11);   // RX, TX

// ── Light threshold ────────────────────────────────────────────────────
const int THRESHOLD  = 620;   // >= THRESHOLD  →  DARK  →  light ON

// Hysteresis stops the relay chattering when the reading sits right on the
// threshold (a bare LDR jitters by a few counts). It gives the OFF edge its
// own trip point at THRESHOLD - HYSTERESIS, so the two edges are:
//
//        >= 620        →  DARK    →  light ON
//     616 .. 619       →  hold whatever it already was
//        <= 615        →  BRIGHT  →  light OFF
//
// So the light stays OFF all the way up to 615 and only comes on at 620.
// Set HYSTERESIS to 0 for a literal single-point threshold at 620.
const int HYSTERESIS = 4;

// A reading has to hold its new value for this many consecutive samples
// before the light is switched. 3 x 100 ms = 300 ms of debounce.
const uint8_t DEBOUNCE_SAMPLES = 3;

// How often the LDR is sampled. Sampling is cheap; SWITCHING is what we
// keep event-driven.
const unsigned long SAMPLE_MS = 100;

// ── Control mode (RAM only — see header note) ──────────────────────────
enum Mode { MODE_AUTO, MODE_MANUAL_ON, MODE_MANUAL_OFF };
Mode currentMode = MODE_AUTO;

// ── State ──────────────────────────────────────────────────────────────
bool     isDark        = false;   // debounced LDR decision, 1 = dark
bool     lightOn       = false;   // relay + LED state, 1 = on
int      lastRaw       = 0;       // last analogRead value (debug only)

bool     pendingDark   = false;   // candidate decision being debounced
uint8_t  pendingCount  = 0;

unsigned long lastSampleAt = 0;

// Incoming command line buffer (non-blocking reader)
char    cmdBuf[16];
uint8_t cmdLen = 0;

// ════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(9600);        // USB debug monitor
  espSerial.begin(9600);     // link to the ESP8266

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN,   OUTPUT);

  // Safe start: relay OFF, LED OFF — BEFORE anything else.
  // With ACTIVE LOW, applyOutputs(false) drives D7 HIGH → coil off → safe.
  applyOutputs(false);
  lightOn = false;

  Serial.println(F("[ARDUINO] ================================"));
  Serial.println(F("[ARDUINO]  Smart Automatic Light Control"));
  Serial.println(F("[ARDUINO] ================================"));
  Serial.print  (F("[ARDUINO] Relay polarity : ACTIVE LOW\n"));
  Serial.print  (F("[ARDUINO] Threshold : "));
  Serial.println(THRESHOLD);
  Serial.print  (F("[ARDUINO] Hysteresis: "));
  Serial.println(HYSTERESIS);

  // Seed the debounced decision from the very first reading so we do not
  // have to wait 300 ms before the light is correct, then enter AUTO.
  lastRaw     = analogRead(LDR_PIN);
  isDark      = (lastRaw >= THRESHOLD);
  pendingDark = isDark;

  enterAuto();               // establishes AUTO and applies the LDR result
  sendStatus();              // announce the boot state to the ESP
}

// ════════════════════════════════════════════════════════════════════════
//  MAIN LOOP — no delay(), nothing blocks
// ════════════════════════════════════════════════════════════════════════
void loop() {
  readEspCommands();         // event: a command arrived from the ESP
  sampleLdr();               // event: the dark/bright decision flipped
}

// ════════════════════════════════════════════════════════════════════════
//  COMMAND READER — non-blocking, one character per pass
// ════════════════════════════════════════════════════════════════════════
void readEspCommands() {
  while (espSerial.available()) {
    char c = espSerial.read();

    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        handleCommand(cmdBuf);
        cmdLen = 0;
      }
      continue;
    }

    // Ignore anything that would overflow the buffer.
    if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

void handleCommand(const char* cmd) {
  Serial.print(F("[ESP->ARD] "));
  Serial.println(cmd);

  if (strcmp(cmd, "STATUS") == 0) {
    sendStatus();                     // just report, change nothing

  } else if (strcmp(cmd, "ON") == 0) {
    currentMode = MODE_MANUAL_ON;     // MANUAL ON — the LDR is ignored
    Serial.println(F("[MODE] MANUAL ON"));
    setLight(true);
    sendStatus();                     // always confirm

  } else if (strcmp(cmd, "OFF") == 0) {
    currentMode = MODE_MANUAL_OFF;    // MANUAL OFF — the LDR is ignored
    Serial.println(F("[MODE] MANUAL OFF"));
    setLight(false);
    sendStatus();

  } else if (strcmp(cmd, "AUTO") == 0) {
    enterAuto();
    sendStatus();

  } else {
    Serial.println(F("[WARN] Unknown command ignored"));
  }
}

// ─── Return to automatic LDR control and act on it immediately ─────────
void enterAuto() {
  currentMode = MODE_AUTO;
  Serial.println(F("[MODE] AUTO"));

  // Re-read the LDR right now so AUTO takes effect without waiting for
  // the next debounce window.
  lastRaw      = analogRead(LDR_PIN);
  isDark       = (lastRaw >= THRESHOLD);
  pendingDark  = isDark;
  pendingCount = 0;

  setLight(isDark);            // dark -> ON, bright -> OFF
}

// ════════════════════════════════════════════════════════════════════════
//  LDR SAMPLING — decides dark/bright, only acts on a real flip
// ════════════════════════════════════════════════════════════════════════
void sampleLdr() {
  if (millis() - lastSampleAt < SAMPLE_MS) return;
  lastSampleAt = millis();

  lastRaw = analogRead(LDR_PIN);

  // Hysteresis band: entering DARK needs >= THRESHOLD, leaving DARK needs
  // < THRESHOLD - HYSTERESIS.
  bool candidate = isDark;
  if (isDark) {
    if (lastRaw <  THRESHOLD - HYSTERESIS) candidate = false;
  } else {
    if (lastRaw >= THRESHOLD)              candidate = true;
  }

  // Debounce: the candidate must repeat before we believe it.
  if (candidate != isDark) {
    if (candidate == pendingDark) {
      pendingCount++;
    } else {
      pendingDark  = candidate;
      pendingCount = 1;
    }

    if (pendingCount < DEBOUNCE_SAMPLES) return;

    // ── EVENT: the LDR decision flipped ─────────────────────────────
    isDark       = candidate;
    pendingCount = 0;

    Serial.print(F("[LDR] raw="));
    Serial.print(lastRaw);
    Serial.println(isDark ? F("  -> DARK") : F("  -> BRIGHT"));

    if (currentMode == MODE_AUTO) {
      setLight(isDark);          // dark -> ON, bright -> OFF
    } else {
      // MANUAL override is active: the LDR must NOT move the light.
      Serial.println(F("[LDR] change ignored — MANUAL override active"));
      sendStatus();              // F1 changed, so the ESP still needs it
    }

  } else {
    pendingDark  = candidate;
    pendingCount = 0;
  }
}

// ════════════════════════════════════════════════════════════════════════
//  OUTPUT CONTROL
// ════════════════════════════════════════════════════════════════════════

// Switches the relay + LED. Returns without touching the pins (and without
// emitting an event) when the requested state is already active — this is
// what makes the sketch event-actuated rather than a polling writer.
void setLight(bool on) {
  if (on == lightOn) return;      // no change -> no event

  lightOn = on;
  applyOutputs(on);

  Serial.print(F("[LIGHT] "));
  Serial.print(on ? F("ON ") : F("OFF"));
  Serial.print(F("  (relay pin "));
  Serial.print(relayLevel(on) == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F(", LED pin "));
  Serial.print(on ? F("HIGH") : F("LOW"));
  Serial.println(')');

  sendStatus();                   // push the change to the ESP at once
}

// The level RELAY_PIN must be driven to for the requested relay state.
uint8_t relayLevel(bool on) {
  if (RELAY_ACTIVE_LOW) return on ? LOW : HIGH;
  return on ? HIGH : LOW;
}

// Raw pin writes. The relay and the LED are driven from the SAME `on` flag
// and only from here, so the bulb and the D13 indicator can never disagree.
// Relay polarity follows RELAY_ACTIVE_LOW; D13 is ACTIVE HIGH.
void applyOutputs(bool on) {
  digitalWrite(RELAY_PIN, relayLevel(on));
  digitalWrite(LED_PIN,   on ? HIGH : LOW);    // active HIGH LED
}

// ════════════════════════════════════════════════════════════════════════
//  STATUS REPORT  →  LDR:x,RELAY:x,LED:x
//  Sent as the reply to STATUS and pushed unsolicited on every change.
// ════════════════════════════════════════════════════════════════════════
void sendStatus() {
  int ldrVal   = isDark  ? 1 : 0;
  int relayVal = (digitalRead(RELAY_PIN) == relayLevel(true)) ? 1 : 0;
  int ledVal   = (digitalRead(LED_PIN)   == HIGH) ? 1 : 0;

  String resp = "LDR:"    + String(ldrVal)
              + ",RELAY:" + String(relayVal)
              + ",LED:"   + String(ledVal);

  espSerial.println(resp);

  Serial.print(F("[ARD->ESP] "));
  Serial.print(resp);
  Serial.print(F("   (mode="));
  Serial.print(currentMode == MODE_AUTO       ? F("AUTO")
             : currentMode == MODE_MANUAL_ON  ? F("MANUAL_ON")
                                              : F("MANUAL_OFF"));
  Serial.print(F(", raw="));
  Serial.print(lastRaw);
  Serial.println(')');
}
