#include <WiFi.h>
#include <ThingSpeak.h>
#include "DHT.h"

/* ================== WiFi Credentials ================== */
const char* ssid = "NU-Guest";
const char* password = "nuTech@0ct23";

/* ================== ThingSpeak ================== */
WiFiClient client;
unsigned long channelID =2841130;   // <-- PUT YOUR CHANNEL ID
const char* writeAPIKey = "W9MFJBKE7Y7DLDIT";

/* ================== DHT Sensor ================== */
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* ================== Voltage Sensor ================== */
#define VOLTAGE_PIN 34
#define ADC_MAX 4095.0
#define VREF 3.3
#define VOLTAGE_DIVIDER_RATIO 7.27 // Calibrated: 24V/3.3V

/* ================== Timing ================== */
unsigned long lastSend = 0;
const unsigned long sendInterval = 300000;  // 5min (ThingSpeak limit)

/* ===================================================== */

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi failed!");
  }
}

/* ===================================================== */

float readVoltage() {
  const int samples = 10;
  long sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += analogRead(VOLTAGE_PIN);
    delay(5);
  }

  float avgADC = sum / (float)samples;
  float adcVoltage = (avgADC / ADC_MAX) * VREF;
  return adcVoltage * VOLTAGE_DIVIDER_RATIO;
}

/* ===================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);
  dht.begin();

  connectWiFi();
  ThingSpeak.begin(client);

  Serial.println("=== Solar Monitoring System Ready ===");
}

/* ===================================================== */

void loop() {

  if (millis() - lastSend >= sendInterval) {

    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {

      float humidity = dht.readHumidity();
      float temperature = dht.readTemperature();
      float voltage = readVoltage();

      if (isnan(humidity) || isnan(temperature)) {
        Serial.println("❌ DHT Sensor Error");
        return;
      }

      Serial.println("------ Solar Panel Data ------");
      Serial.print("Voltage      : "); Serial.print(voltage); Serial.println(" V");
      Serial.print("Temperature  : "); Serial.print(temperature); Serial.println(" °C");
      Serial.print("Humidity     : "); Serial.print(humidity); Serial.println(" %");
      Serial.println("--------------------------------");

      ThingSpeak.setField(1, temperature);
      ThingSpeak.setField(2, humidity);
      ThingSpeak.setField(3, voltage);

      int response = ThingSpeak.writeFields(channelID, writeAPIKey);

      if (response == 200) {
        Serial.println("✅ Data sent to ThingSpeak");
      } else {
        Serial.print("❌ ThingSpeak Error: ");
        Serial.println(response);
      }
    }

    lastSend = millis();
  }
}
