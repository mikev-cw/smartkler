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
extern String deviceID;
extern String deviceIP;
extern const int pinIgro;
extern const int pinRelay;

// Soil moisture sensor
extern int soilMoistureCalibrationMin;
extern int soilMoistureCalibrationMax;
extern unsigned long lastMoistureReadTime;
extern StaticJsonDocument<128> lastMoistureData;
extern unsigned long soilReadsIntervalMs;

// Relay
extern unsigned long lastValveStartTime;
extern const unsigned long valveSecurityStop;
extern unsigned long valveDurationMs;

// Utils
String getUptime();
extern unsigned long GetEpochTime();
extern unsigned int defaultDurationMinutes;
extern unsigned int defaultMoistureLimit;
extern unsigned long lastSensorInfoPublished;
extern unsigned long sensorInfoPublishIntervalMs;

#endif