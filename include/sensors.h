#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <ArduinoJson.h>

StaticJsonDocument<128>& readSoilMoisture (bool forceRead);
StaticJsonDocument<64>& readRelayState();
int setRelayState(bool state);
void checkValveWatchdog();

#endif