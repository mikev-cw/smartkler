#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

void connectToWiFi();
void checkWiFiConnection();
String getDeviceIP();

#endif