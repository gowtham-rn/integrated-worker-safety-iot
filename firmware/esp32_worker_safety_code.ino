#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- WIFI ----------------
const char* ssid = "your wifi name";
const char* password = "your wifi password";

String serverURL = "IP address";

// ---------------- SENSORS ----------------
MAX30105 particleSensor;
Adafruit_MPU6050 mpu;

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

#define MQ7_PIN 34
#define MQ135_PIN 35

#define LED 25

// -------- Timing (Non-blocking) --------
unsigned long previousMillis = 0;
const unsigned long interval = 2000;

// -------- Warning Blink --------
unsigned long ledMillis = 0;
bool ledState = false;

// ----------------------------------------

void setup() {

  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // -------- WiFi --------
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  // -------- I2C --------
  Wire.begin();

  if (!particleSensor.begin(Wire)) {
    Serial.println("MAX30102 not detected");
  } else {
    Serial.println("MAX30102 initialized");
  }

  if (!mpu.begin()) {
    Serial.println("MPU6050 not detected");
  } else {
    Serial.println("MPU6050 ready");
  }

  tempSensor.begin();
}

// ----------------------------------------

void loop() {

  // -------- WiFi Reconnect --------
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    WiFi.begin(ssid, password);
    return;
  }

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {

    previousMillis = currentMillis;

    // -------- Simulated Heart Data --------
    float heart_rate = random(70, 95);
    float spo2 = random(94, 100);

    // -------- Temperature --------
    tempSensor.requestTemperatures();
    float temperature = tempSensor.getTempCByIndex(0);

    if (temperature == -127.0) {
      Serial.println("Temperature sensor error!");
      temperature = 0;
    }

    // -------- MQ Sensors --------
    int mq7_raw = analogRead(MQ7_PIN);
    int mq135_raw = analogRead(MQ135_PIN);

    int co_level = mq7_raw / 25;
    int air_quality = mq135_raw / 15;

    // -------- MPU6050 --------
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float accel_x = a.acceleration.x;
    float accel_y = a.acceleration.y;
    float accel_z = a.acceleration.z;

    // -------- Debug --------
    Serial.println("----------- SENSOR DATA -----------");
    Serial.println("Heart Rate: " + String(heart_rate));
    Serial.println("SpO2: " + String(spo2));
    Serial.println("Temperature: " + String(temperature));
    Serial.println("CO Level: " + String(co_level));
    Serial.println("Air Quality: " + String(air_quality));
    Serial.println("-----------------------------------");

    // -------- Create JSON --------
    StaticJsonDocument<512> doc;

    doc["device_id"] = "esp32_01";
    doc["heart_rate"] = heart_rate;
    doc["spo2"] = spo2;
    doc["temperature"] = temperature;
    doc["co_level"] = co_level;
    doc["air_quality"] = air_quality;
    doc["accel_x"] = accel_x;
    doc["accel_y"] = accel_y;
    doc["accel_z"] = accel_z;

    String jsonData;
    serializeJson(doc, jsonData);

    // -------- Send to Flask --------
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode > 0) {

      String response = http.getString();
      Serial.println("Server Response: " + response);

      StaticJsonDocument<128> resDoc;
      DeserializationError error = deserializeJson(resDoc, response);

      if (!error) {
        int status_code = resDoc["status_code"];
        handleAlert(status_code);
      } else {
        Serial.println("JSON parse error");
      }

    } else {
      Serial.println("HTTP Error: " + String(httpResponseCode));
    }

    http.end();
  }
}

// -------- ALERT SYSTEM --------

void handleAlert(int code) {

  if (code == 0) {
    Serial.println("STATUS: SAFE");
    digitalWrite(LED, LOW);
  }

  else if (code == 1) {
    Serial.println("STATUS: WARNING (Blinking)");

    if (millis() - ledMillis >= 500) {
      ledMillis = millis();
      ledState = !ledState;
      digitalWrite(LED, ledState);
    }
  }

  else if (code == 2) {
    Serial.println("STATUS: CRITICAL");
    digitalWrite(LED, HIGH);
  }
}
