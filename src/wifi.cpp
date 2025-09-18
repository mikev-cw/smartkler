#include <Arduino.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "globals.h"

String getDeviceIP()
{
  return WiFi.localIP().toString();
}

void connectToWiFi()
{
  WiFiManager wm;

  String device_id = String(ESP.getChipId(), HEX); // ID in esadecimale
  device_id.toUpperCase();                         // opzionale: maiuscolo

  String portalName = "SmartklerSetup-" + device_id;

  Serial.println("SSID captive portal: " + portalName);

  if (!wm.autoConnect(portalName.c_str()))
  {
    Serial.println("Failed to connect. Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected.");
  Serial.print("IP Address: ");
  deviceIP = getDeviceIP();
  Serial.println(deviceIP);
}

void checkWiFiConnection()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected. Attempting to reconnect...");
    connectToWiFi();
  }
}