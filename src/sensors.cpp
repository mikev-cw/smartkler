#include <ArduinoJson.h>
#include "sensors.h"
#include "globals.h"
#include "mqtt.h"

StaticJsonDocument<128>& readSoilMoisture(bool forceRead = false)
{
    unsigned long now = millis();

    if (!forceRead && (now - lastMoistureReadTime < minReadIntervalMs))
    {
        Serial.println("Serving last soil read (too recent)");
        return lastMoistureReading;
    }

    int raw = analogRead(pinIgro);

    // Map raw value to percentage (adjust min/max based on real sensor)
    int percent = map(raw, SOIL_MOISTURE_RAW_MAX, SOIL_MOISTURE_RAW_MIN, 0, 100);

    // Clamp the value between 0 and 100
    percent = constrain(percent, 0, 100); // TOD verify this

    // return JSONED raw value, percent value
    Serial.print("Raw Soil Moisture: ");
    Serial.print(raw);
    Serial.print(" | Mapped to Percent: ");
    Serial.print(percent);
    Serial.println();

    static StaticJsonDocument<128> doc;
    doc["raw"] = raw;
    doc["percent"] = percent;
    doc["timestamp"] = now;
    doc["raw_mapper_min"] = SOIL_MOISTURE_RAW_MIN;
    doc["raw_mapper_max"] = SOIL_MOISTURE_RAW_MAX;

    lastMoistureReading = doc;
    lastMoistureReadTime = now;

    return doc;
}

StaticJsonDocument<64>& readRelayState()
{
    int state = digitalRead(pinRelay);

    static StaticJsonDocument<64> doc;
    doc["relay_state"] = state; // 0 = closed, 1 = opened (logical)

    return doc;
}

int setRelayState(bool state)
{
    digitalWrite(pinRelay, state ? HIGH : LOW);
    StaticJsonDocument<128> msg;

    if (state)
    {
        Serial.println("Valve turned ON (watchdog started)");
        msg["command_result"] = "valve_on";
        msg["message"] = "Valvola aperta";
    }
    else
    {
        Serial.println("Valve turned OFF");
        msg["command_result"] = "valve_off";
        msg["message"] = "Valvola chiusa";
    }
    
    mqttPublish(topics.valve.c_str(), msg);

    int newstate = digitalRead(pinRelay);

    StaticJsonDocument<64> maindoc;
    JsonObject relayObj = maindoc.createNestedObject("relay");
    relayObj["relay_state"] = newstate;
    mqttPublish(topics.sensors.c_str(), maindoc);

    return newstate; // Return the new state
}

void checkValveWatchdog()
{
    bool relayState = digitalRead(pinRelay) == HIGH; // or LOW if your relay is active-low
    
    if (!relayState)
        return; // No need to check if valve is off

    unsigned long now = millis();
    bool shouldStop = false;
    String reason;
    
    // 1. Duration expired
    if (now - valveLastStartTime >= valveDurationMs)
    {
        shouldStop = true;
        reason = "Regular time expired";
    }

    if (now - valveLastStartTime > valveSecurityStop)
    {
        shouldStop = true;
        reason = "Watchdog security Timeout";
    }

    if (shouldStop)
    {
        Serial.printf("[VALVE] Auto-off triggered: reason = %s\n", reason.c_str());
        setRelayState(false);

        StaticJsonDocument<128> msg;
        msg["command_result"] = "valve_auto_off";
        msg["reason"] = reason;
        mqttPublish(topics.systemEvents.c_str(), msg);
    }
}