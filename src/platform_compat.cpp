#include <Arduino.h>
#include "globals.h"

String getDeviceId()
{
#if defined(ESP8266)
  return String(ESP.getChipId(), HEX);
#elif defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  return String((uint32_t)(mac >> 24), HEX);
#else
  return String("unknown");
#endif
}
