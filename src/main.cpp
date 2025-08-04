// Libs
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <fauxmoESP.h>

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
unsigned long valveDurationMs = 0; // Duration setted for which the valve should be open (in milliseconds)

// Defaults
const unsigned long valveSecurityStop = 45UL * 60UL * 1000UL; // 45 minutes
unsigned int defaultDurationMinutes = 10; // Default value when Valve turned on without a duration
unsigned int defaultMoistureLimit = 150; // Default value for skipping irrigation if soil moisture is above limit when valve is turned on without a limit
int soilMoistureCalibrationMin = 300;
int soilMoistureCalibrationMax = 1023;

// Intervals
const unsigned long loopIntervalMs = 2UL * 1000UL;                  // Loop interval
unsigned long sensorInfoPublishIntervalMs = 10UL * 60UL * 1000UL;   // Sensor data publishing interval
unsigned long soilReadsIntervalMs = 5UL * 60UL * 1000UL;            // minimum interval between every soil moisture reads

// Timings 
unsigned long lastLoopTick = 0;
unsigned long lastValveStartTime = 0;
unsigned long lastSensorInfoPublished = 0;
unsigned long lastMoistureReadTime = 0; // Last millis soil moisture was read
StaticJsonDocument<128> lastMoistureData;

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
        publishSystemEvent("OTA Update Started", "ota_start");
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

  // Alexa related setup
  fauxmoSetup();

  // OTA setup
  OTASetup();

  Serial.println("Device ID: " + device_id);
  Serial.printf("Device IP: %s\n", WiFi.localIP().toString().c_str());

  publishSensorData(true); // Initial read to set min/max values
  publishSystemEvent("Smartkler Started", "system_started");
}

void loop()
{
    mqttClient.loop();
    ArduinoOTA.handle();
    fauxmo.handle();
    checkValveWatchdog();

    unsigned long now = millis();

    if (now - lastSensorInfoPublished >= sensorInfoPublishIntervalMs)
    {
        lastSensorInfoPublished = now;
        publishSensorData();
    }

    if (now - lastLoopTick < loopIntervalMs)
        return; // throttle heavy checks

    lastLoopTick = now;
    checkWiFiConnection();
    checkMQTTConnection();
}