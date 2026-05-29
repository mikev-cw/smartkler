// Libs
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
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
#include "wifi_utils.h"
#include "mqtt.h"

// Global defines
String deviceID;
String deviceIP;
Topics topics;

#ifndef PIN_IGRO
#if defined(ESP8266)
#define PIN_IGRO A0
#elif defined(ESP32)
#define PIN_IGRO 34
#endif
#endif

#ifndef PIN_RELAY
#if defined(ESP8266)
#define PIN_RELAY D6
#elif defined(ESP32)
#define PIN_RELAY 26
#endif
#endif

const int pinIgro = PIN_IGRO;  // igro
const int pinRelay = PIN_RELAY; // valve relay
unsigned long valveDurationMs = 0; // Duration setted for which the valve should be open (in milliseconds)

// Defaults
const unsigned long valveSecurityStop = 45UL * 60UL * 1000UL; // 45 minutes
unsigned int defaultDurationMinutes = 10; // Default value when Valve turned on without a duration
unsigned int defaultMoistureLimit = 150; // Default value for skipping irrigation if soil moisture is above limit when valve is turned on without a limit
#if defined(ESP8266)
int soilMoistureCalibrationMin = 300;
int soilMoistureCalibrationMax = 1023;
#elif defined(ESP32)
int soilMoistureCalibrationMin = 1200;
int soilMoistureCalibrationMax = 4095;
#endif

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
    ArduinoOTA.setHostname(("smartkler-" + deviceID).c_str());
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

    fauxmo.addDevice(("Irrigatore " + deviceID).c_str()); // Device name that will appear in Alexa app

    fauxmo.onSetState([](unsigned char device_id, const char *device_name, bool state, unsigned char value)
    {
      Serial.printf("[FAUXMO] %s -> %s\n", device_name, state ? "ON" : "OFF");
      Serial.printf("[FAUXMO] Device ID: %d, Value: %d\n", device_id, value);

      if (state)
      {
        valveDurationMs = (unsigned long)defaultDurationMinutes * 60000UL;
        lastValveStartTime = millis();
      }

      setRelayState(state);
    });
}

void setup()
{
  Serial.begin(115200);
#if defined(ESP8266)
  Serial.setDebugOutput(true);
#elif defined(ESP32)
  analogReadResolution(12);
#endif

  deviceID = getDeviceId();
  deviceID.toUpperCase();

  topics = {
      "smartkler/commands/" + deviceID,
      "smartkler/systemEvents/" + deviceID,
      "smartkler/data/" + deviceID,
      "smartkler/valve/" + deviceID,
      "smartkler/lwt/" + deviceID,
  };

  pinMode(pinIgro, INPUT);
  pinMode(pinRelay, OUTPUT);

  // Deactivate Relay on startup to ensure valve is closed when system reboots
  digitalWrite(pinRelay, LOW);

  connectToWiFi();
  connectToMQTT();

  // Handle WiFi connection events
#if defined(ESP8266)
  WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    Serial.printf("WiFi disconnected: reason=%d, RSSI=%d\n", event.reason, WiFi.RSSI());
    WiFi.begin(); 
  });

  WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
    deviceIP = getDeviceIP();
    Serial.println("WiFi Reconnected. IP: " + deviceIP);
  });
#elif defined(ESP32)
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("WiFi disconnected: reason=%d, RSSI=%d\n", info.wifi_sta_disconnected.reason, WiFi.RSSI());
    WiFi.reconnect();
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    deviceIP = getDeviceIP();
    Serial.println("WiFi Reconnected. IP: " + deviceIP);
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
#endif

  // Alexa related setup
  fauxmoSetup();

  // OTA setup
  OTASetup();

  Serial.println("Device ID: " + deviceID);
  Serial.printf("Device IP: %s\n", deviceIP.c_str());

  publishSensorData(true); // Initial read to set min/max values
  publishSystemEvent("Smartkler Started", "system_started");
}

void loop()
{
    mqttClient.loop();
    ArduinoOTA.handle();
    fauxmo.handle();
    checkValveWatchdog();
    processDeferredSensorPublish();

    unsigned long now = millis();

    if (now - lastSensorInfoPublished >= sensorInfoPublishIntervalMs)
    {
        lastSensorInfoPublished = now;
        publishSensorData();
    }

    // heavy checks throttled
    if (now - lastLoopTick >= loopIntervalMs)
    {
        lastLoopTick = now;
        checkWiFiConnection();
        checkMQTTConnection();
    }
}
