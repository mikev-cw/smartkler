#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>

extern PubSubClient mqttClient;

void connectToMQTT();
void checkMQTTConnection();
void mqttSubscribe(const char* topic);
void mqttPublish(const char *topic, const JsonDocument &payload);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void publishSensorData(bool calibrate = false);
void publishSystemEvent(const char *action, const char *actionCode);

#endif