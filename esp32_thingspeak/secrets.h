// ════════════════════════════════════════════════════════════════════════
//  SECRETS — WiFi + ThingSpeak credentials for esp8266_thingspeak.ino
//  DO NOT COMMIT THIS FILE. It is ignored via .gitignore.
//  The Arduino IDE copies it next to the sketch during compilation.
// ════════════════════════════════════════════════════════════════════════

// WiFi network (must be 2.4 GHz — the ESP8266 cannot see 5 GHz).
const char* WIFI_SSID     = "NU-Guest";
const char* WIFI_PASS     = "nuTech@0ct23";

// ThingSpeak channel credentials — from your channel's "API Keys" tab.
const char* TS_WRITE_KEY  = "X44JF7GRC3T1ZY9K";  // ← regenerate if ever exposed
const char* TS_READ_KEY   = "Q06MR4N2DK6LG2OP";
const char* TS_CHANNEL    = "3245023";
