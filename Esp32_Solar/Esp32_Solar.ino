/*
  ESP32 Voltage + DHT11 → ThingSpeak
  Field1: Temperature
  Field2: Humidity
  Field3: Voltage
*/

#include <WiFi.h>
#include <ThingSpeak.h>
#include "DHT.h"

/* ========== WiFi Credentials ========== */
const char* ssid = "ECE-Lab";
const char* password = "nuTech@0ct23";

/* ========== ThingSpeak ========== */
WiFiClient client;
unsigned long channelID = 2841130;
const char* writeAPIKey = "W9MFJBKE7Y7DLDIT";

/* ========== DHT11 ========== */
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* ========== Voltage Measurement ========== */
#define VIN_PIN 35
#define ADC_MAX 4095.0
#define VREF 3.3

#define R1 100000.0
#define R2 10000.0
#define DIVIDER_RATIO ((R1 + R2) / R2)
#define CALIBRATION_FACTOR 1.08   // Adjust once if needed

/* ========== Timing ========== */
unsigned long lastSend = 0;
const unsigned long sendInterval = 20000; // ThingSpeak limit (20 sec)

/* ====================================== */

float readVoltage() {
  long sum = 0;

  for (int i = 0; i < 50; i++) {
    sum += analogRead(VIN_PIN);
    delayMicroseconds(200);
  }

  float adcValue = sum / 50.0;
  float adcVoltage = (adcValue / ADC_MAX) * VREF;
  float inputVoltage = adcVoltage * DIVIDER_RATIO * CALIBRATION_FACTOR;

  if (inputVoltage < 0) inputVoltage = 0;
  if (inputVoltage > 25) inputVoltage = 25;

  return inputVoltage;
}

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(VIN_PIN, ADC_11db);

  dht.begin();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  ThingSpeak.begin(client);

  Serial.println("ESP32 DHT11 + Voltage → ThingSpeak Started");
}

void loop() {

  if (millis() - lastSend >= sendInterval) {

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    float voltage = readVoltage();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("❌ Failed to read from DHT11");
      return;
    }

    Serial.println("Sending data to ThingSpeak:");
    Serial.print("Temp: "); Serial.print(temperature); Serial.println(" °C");
    Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");
    Serial.print("Voltage: "); Serial.print(voltage); Serial.println(" V");

    ThingSpeak.setField(1, temperature);
    ThingSpeak.setField(2, humidity);
    ThingSpeak.setField(3, voltage);

    int response = ThingSpeak.writeFields(channelID, writeAPIKey);

    if (response == 200) {
      Serial.println("✅ Data sent successfully");
    } else {
      Serial.print("❌ ThingSpeak error: ");
      Serial.println(response);
    }

    lastSend = millis();
  }
}
