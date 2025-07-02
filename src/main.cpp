// Libs
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <fauxmoESP.h>

// Config
unsigned long epochTime; // Variable to hold current epoch timestamp

// Modules inits
fauxmoESP fauxmo;                   // Alexa
WiFiClient espClient;               // WiFi
PubSubClient mqttClient(espClient); // MQTT
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "it.pool.ntp.org");

// Includes
#include "globals.h"
#include "sensors.h"
#include "wifi.h"
#include "mqtt.h"

// Global defines
String device_id;
Topics topics;
const int pinIgro = A0;  // igro
const int pinRelay = D6; // valve relay
int SOIL_MOISTURE_RAW_MIN = 300;
int SOIL_MOISTURE_RAW_MAX = 1023;
unsigned long lastMoistureReadTime = 0;
StaticJsonDocument<128> lastMoistureReading;
unsigned long minReadIntervalMs = 300000; // 5 minutes // minimum interval between soil moisture reads
unsigned long valveLastStartTime = 0;
const unsigned long valveSecurityStop = 45UL * 60UL * 1000UL; // 45 minutes
unsigned int defaultDurationMinutes = 10;
unsigned int defaultMoistureLimit = 150;
unsigned long valveDurationMs = 0;
unsigned long lastLoopTick = 0;
const unsigned long loopIntervalMs = 1000; // Loop interval
unsigned long lastSensorInfoPublish = 0;
unsigned long sensorInfoInterval = 10UL * 60UL * 1000UL; // 10 minutes

unsigned long GetEpochTime()
{
  timeClient.update();
  unsigned long now = timeClient.getEpochTime();
  return now;
}

String getUptime()
{
    unsigned long ms = millis() / 1000;
    unsigned int days = ms / 86400;
    unsigned int hours = (ms % 86400) / 3600;
    unsigned int minutes = (ms % 3600) / 60;
    unsigned int seconds = ms % 60;

    char formatted[20];

    if (days > 0)
    {
        sprintf(formatted, "%u days %02u:%02u:%02u", days, hours, minutes, seconds);
    }
    else
    {
        sprintf(formatted, "%02u:%02u:%02u", hours, minutes, seconds);
    }

    return formatted;
}

void OTASetup() {
    ArduinoOTA.setHostname(("smartkler-" + device_id).c_str());
    ArduinoOTA.onStart([]()
    { 
        StaticJsonDocument<64> otaDoc;
        otaDoc["action"] = "OTA Update Started";
        otaDoc["action_code"] = "ota_start";
        mqttPublish(topics.systemEvents.c_str(), otaDoc);
        Serial.println("OTA Start");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("OTA End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]\n", error);
    });

    ArduinoOTA.begin();
}

void fauxmoSetup() {
    fauxmo.createServer(true); // Internal web server
    fauxmo.setPort(80);        // Required for Alexa
    fauxmo.enable(true);

    fauxmo.addDevice("Smartkler 2.0");

    fauxmo.onSetState([](unsigned char device_id, const char *device_name, bool state, unsigned char value)
    {
      Serial.printf("[FAUXMO] %s -> %s\n", device_name, state ? "ON" : "OFF");
      Serial.printf("[FAUXMO] Device ID: %d, Value: %d\n", device_id, value);
      setRelayState(state);
    });
}

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); // Italian timezone
  tzset();

  device_id = (String)ESP.getChipId();

  topics = {
      "smartkler/commands/" + device_id,
      "smartkler/systemEvents/" + device_id,
      "smartkler/data/" + device_id,
      "smartkler/valve/" + device_id,
      "smartkler/lwt/" + device_id,
  };

  pinMode(pinIgro, INPUT);
  pinMode(pinRelay, OUTPUT);

  // Deactivate Relay on startup to ensure valve is closed when system reboots
  digitalWrite(pinRelay, LOW);

  connectToWiFi();
  connectToMQTT();

  // handle WiFi connection events
  WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    Serial.println("WiFi disconnected. Trying to reconnect...");
    WiFi.begin(); 
  });

  WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) { 
    Serial.println("WiFi Reconnected. IP: " + getDeviceIP()); 
  });

  epochTime = GetEpochTime();

  // Alexa related setup
  fauxmoSetup();

  // OTA setup
  OTASetup();

  Serial.println("Device ID: " + device_id);
  Serial.printf("Device IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Epoch Time: %lu\n", epochTime);

  readSoilMoisture(true); // Initial read to set min/max values // TODO send MQTT sensors

  StaticJsonDocument<96> responseDocEvt;
  responseDocEvt["action"] = "Smartkler Started";
  responseDocEvt["action_code"] = "system_started";
  mqttPublish(topics.systemEvents.c_str(), responseDocEvt);
}

void loop()
{
    unsigned long now = millis();

    if (now - lastLoopTick < loopIntervalMs)
    {
        return; // too early, skip this loop cycle
    }

    if (now - lastSensorInfoPublish >= sensorInfoInterval)
    {
        lastSensorInfoPublish = now;

        StaticJsonDocument<256> responseDoc;
        responseDoc["igro"] = readSoilMoisture(false); 
        responseDoc["relay"] = readRelayState();

        mqttPublish(topics.data.c_str(), responseDoc);
    }

    lastLoopTick = now;

    checkWiFiConnection();
    checkMQTTConnection();
    checkValveWatchdog();

    ArduinoOTA.handle();
    fauxmo.handle();
}