#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

struct Topics
{
    String commands; // Where commands are received
    String systemEvents; // System events like boot, errors, commands responses, etc.
    String data; // Where sensors data and system configs are sent
    String valve; // Relay history events
    String lwt; // Last Will and Testament topic for MQTT
};

extern Topics topics;
extern String device_id;
extern const int pinIgro;
extern const int pinRelay;

// Soil moisture sensor
extern int SOIL_MOISTURE_RAW_MIN;
extern int SOIL_MOISTURE_RAW_MAX;
extern unsigned long lastMoistureReadTime;
extern StaticJsonDocument<128> lastMoistureReading;
extern unsigned long minReadIntervalMs;

// Relay
extern unsigned long valveLastStartTime;
extern const unsigned long valveSecurityStop;
extern unsigned long valveDurationMs;

// Utils
String getUptime();
extern unsigned long GetEpochTime();
extern unsigned int defaultDurationMinutes;
extern unsigned int defaultMoistureLimit;
extern unsigned long lastSensorInfoPublish;
extern unsigned long sensorInfoInterval;

#endif