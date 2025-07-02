#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <map>
#include <functional>
#include <ArduinoJson.h>
#include "mqtt.h"
#include "globals.h"
#include "sensors.h"
#include "secrets.h"

// Forward declaration for sensors functions
int setRelayState(bool state);

std::map<String, std::function<void(const JsonDocument &)>> commandHandlers;

// Client WiFi e MQTT
extern WiFiClient espClient;
extern PubSubClient mqttClient;

String clientId = "Smartkler-" + String(ESP.getChipId(), HEX);

void initMQTThandlers()
{
  commandHandlers["setIgroMappers__DEPRECATED"] = [](const JsonDocument &doc)
  {
    int oldMin = SOIL_MOISTURE_RAW_MIN;
    int oldMax = SOIL_MOISTURE_RAW_MAX;
    bool changedMin = false;
    bool changedMax = false;

    if (doc.containsKey("min"))
    {
      SOIL_MOISTURE_RAW_MIN = doc["min"];
      changedMin = (SOIL_MOISTURE_RAW_MIN != oldMin);
    }

    if (doc.containsKey("max"))
    {
      SOIL_MOISTURE_RAW_MAX = doc["max"];
      changedMax = (SOIL_MOISTURE_RAW_MAX != oldMax);
    }

    Serial.println("Soil moisture calibration update requested:");
    if (changedMin)
      Serial.println("  MIN changed: " + String(oldMin) + " → " + String(SOIL_MOISTURE_RAW_MIN));
    if (changedMax)
      Serial.println("  MAX changed: " + String(oldMax) + " → " + String(SOIL_MOISTURE_RAW_MAX));
    if (!changedMin && !changedMax)
      Serial.println("  No changes were made.");

    // Prepare MQTT response
    StaticJsonDocument<128> responseDoc;
    responseDoc["command_result"] = doc;
    responseDoc["with_err"] = false;
    responseDoc["message"] = "";

    if (changedMin)
    {
      responseDoc["min_old"] = oldMin;
      responseDoc["min_new"] = SOIL_MOISTURE_RAW_MIN;
    }

    if (changedMax)
    {
      responseDoc["max_old"] = oldMax;
      responseDoc["max_new"] = SOIL_MOISTURE_RAW_MAX;
    }

    if (!changedMin && !changedMax)
    {
      responseDoc["with_err"] = true;
      responseDoc["message"] = "No calibration values changed.";
    }

    mqttPublish(topics.systemEvents.c_str(), responseDoc);
  };

  commandHandlers["setConfigParam"] = [](const JsonDocument &doc)
  {
    bool anyChange = false;

    StaticJsonDocument<256> responseDoc;
    responseDoc["with_err"] = false;
    responseDoc["message"] = "";

    if (doc.containsKey("igro_min"))
    {
      int newMin = doc["igro_min"];
      if (newMin != SOIL_MOISTURE_RAW_MIN)
      {
        responseDoc["igro_min_old"] = SOIL_MOISTURE_RAW_MIN;
        SOIL_MOISTURE_RAW_MIN = newMin;
        responseDoc["igro_min_new"] = SOIL_MOISTURE_RAW_MIN;
        anyChange = true;
      }
    }

    if (doc.containsKey("igro_max"))
    {
      int newMax = doc["igro_max"];
      if (newMax != SOIL_MOISTURE_RAW_MAX)
      {
        responseDoc["igro_max_old"] = SOIL_MOISTURE_RAW_MAX;
        SOIL_MOISTURE_RAW_MAX = newMax;
        responseDoc["igro_max_new"] = SOIL_MOISTURE_RAW_MAX;
        anyChange = true;
      }
    }

    if (doc.containsKey("moistureSensorInterval_minutes"))
    {
      unsigned long newValMin = doc["moistureSensorInterval_minutes"];
      unsigned long newValMs = newValMin * 60UL * 1000UL;

      if (newValMs != minReadIntervalMs)
      {
        responseDoc["moistureSensorInterval_minutes_old"] = minReadIntervalMs / 60000UL;
        minReadIntervalMs = newValMs;
        responseDoc["moistureSensorInterval_minutes_new"] = newValMin;
        anyChange = true;
      }
    }

    if (doc.containsKey("sensorDataInterval_minutes"))
    {
      unsigned long newValMin = doc["sensorDataInterval_minutes"];
      unsigned long newValMs = newValMin * 60UL * 1000UL;

      if (newValMs != sensorInfoInterval)
      {
        responseDoc["sensorDataInterval_minutes_old"] = sensorInfoInterval / 60000UL;
        sensorInfoInterval = newValMs;
        responseDoc["sensorDataInterval_minutes_new"] = newValMin;
        anyChange = true;
      }
    }

    if (!anyChange)
    {
      responseDoc["with_err"] = true;
      responseDoc["message"] = "No configuration changes applied.";
    }

    mqttPublish(topics.systemEvents.c_str(), responseDoc);
  };
  
  commandHandlers["setValve"] = [](const JsonDocument &doc)
  {
    if (!doc.containsKey("state"))
      return;

    String state = doc["state"];
    state.toLowerCase();

    if (state == "on")
    {
      int minutes = doc["minutes"] | defaultDurationMinutes;
      int maxMoisture = doc["moistureLimit"] | defaultMoistureLimit;

      Serial.printf("Turning valve ON for %d min if soil moisture is < %d%% \n", minutes, maxMoisture);
      valveDurationMs = (unsigned long)minutes * 60000;
      valveLastStartTime = millis(); // Start timer
      setRelayState(true);
    }
    else if (state == "off")
    {
      Serial.println("Turning valve OFF.");
      setRelayState(false);
    }
  };

  commandHandlers["getData"] = [](const JsonDocument &doc)
  {
    StaticJsonDocument<256> responseDoc;
    responseDoc["igro"] = readSoilMoisture(doc.containsKey("force") ? true : false);
    responseDoc["relay"] = readRelayState();

    mqttPublish(topics.sensors.c_str(), responseDoc);
  };

  commandHandlers["shutdown-r"] = [](const JsonDocument &doc) {
    Serial.println("Restarting device...");
    StaticJsonDocument<96> responseDoc;
    responseDoc["action"] = "Smartkler Restarting";
    responseDoc["action_code"] = "system_rebooting";
    mqttPublish(topics.systemEvents.c_str(), responseDoc);
    delay(3000);
    ESP.restart();
  };

  commandHandlers["shutdown-h"] = [](const JsonDocument &doc)
  {
    Serial.println("System Shutdown...");
    StaticJsonDocument<96> responseDoc;
    responseDoc["action"] = "Smartkler Shutting Down";
    responseDoc["action_code"] = "system_shutting_down";
    mqttPublish(topics.systemEvents.c_str(), responseDoc);
    delay(3000);
    ESP.deepSleep(0);
  };

  commandHandlers["ping"] = [](const JsonDocument &doc)
  {
    Serial.println("Ping request received");
    StaticJsonDocument<96> responseDoc;
    responseDoc["action"] = "PONG!";
    responseDoc["action_code"] = "ping_response";
    mqttPublish(topics.systemEvents.c_str(), responseDoc);
  };
};

void connectToMQTT()
  {
    Serial.print("Attempting MQTT connection...");
    if (!mqttClient.connected())
    {
      mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
      mqttClient.setCallback(mqttCallback);

      initMQTThandlers();
      
      if (mqttClient.connect(
            clientId.c_str(),
            MQTT_USERNAME,
            MQTT_PASSWORD,
            topics.lwt.c_str(), // willTopic
            1,                 // willQos
            true,              // willRetain
            "offline"          // willMessage
          ))
      {
        Serial.println("connected to MQTT broker " + String(MQTT_SERVER));
        // Subscribe to topics after successful connection
        mqttSubscribe(topics.commands.c_str());

        StaticJsonDocument<96> doc;
        doc["action"] = "MQTT connected";
        doc["action_code"] = "mqtt_connected";
        mqttPublish(topics.systemEvents.c_str(), doc);
        
        // LWT
        mqttClient.publish(topics.lwt.c_str(), "online", true);
      }
      else
      {
        // RC descriptions

        //-4 : MQTT_CONNECTION_TIMEOUT - the server didn’t respond within the keepalive time
        //-3 : MQTT_CONNECTION_LOST - the network connection was broken
        //-2 : MQTT_CONNECT_FAILED - the network connection failed
        //-1 : MQTT_DISCONNECTED - the client is disconnected cleanly
        // 0 : MQTT_CONNECTED - the client is connected
        // 1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn’t support the requested version of MQTT
        // 2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
        // 3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
        // 4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
        // 5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect

        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        if (mqttClient.state() == -4)
        {
          Serial.print("-MQTT_CONNECTION_TIMEOUT - the server didn’t respond within the keepalive time");
        }
        if (mqttClient.state() == -3)
        {
          Serial.print("-MQTT_CONNECTION_LOST - the MQTT network connection was broken");
        }
        if (mqttClient.state() == -2)
        {
          Serial.print("-MQTT_CONNECT_FAILED - the MQTT network connection failed");
        }
        if (mqttClient.state() == -1)
        {
          Serial.print("-MQTT_DISCONNECTED - the client is disconnected cleanly");
        }
        if (mqttClient.state() == 0)
        {
          Serial.print("-MQTT_CONNECTED - the client is connected");
        }
        if (mqttClient.state() == 1)
        {
          Serial.print("-MQTT_CONNECT_BAD_PROTOCOL - the server doesn’t support the requested version of MQTT");
        }
        if (mqttClient.state() == 2)
        {
          Serial.print("-MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier");
        }
        if (mqttClient.state() == 3)
        {
          Serial.print("-MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection");
        }
        if (mqttClient.state() == 4)
        {
          Serial.print("-MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected");
        }
        if (mqttClient.state() == 4)
        {
          Serial.print("-MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect");
        }

        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    else
    {
      Serial.println("Already connected to MQTT broker.");
    }
}

void checkMQTTConnection()
{
  if (!mqttClient.connected())
  {
    connectToMQTT();
  }
  mqttClient.loop();
}

void mqttSubscribe(const char *topic)
{
  mqttClient.subscribe(topic);
  Serial.println("Subscribed to topic: " + String(topic));
}

void mqttPublish(const char *topic, const JsonDocument &payload)
{
  StaticJsonDocument<512> doc;

  unsigned long now = GetEpochTime();
  char isoTime[25];
  time_t rawtime = (time_t)now;
  struct tm *timeinfo = localtime(&rawtime);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", timeinfo);

  // Build wrapper
  doc["timestamp"] = now;
  doc["datetime"] = isoTime;
  doc["uptime"] = getUptime();

  // Deep copy 'payload' into "data"
  doc["data"] = payload;

  // Serialize and publish
  char buffer[512];
  size_t len = serializeJson(doc, buffer);
  mqttClient.publish(topic, buffer, len);
  Serial.println("Published to " + String(topic) + ": " + payload.as<String>());
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  StaticJsonDocument<256> doc;

  // Deserialize the JSON
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    Serial.println("Payload: ");
    for (unsigned int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    return;
  }

  if (!doc.containsKey("command"))
  {
    Serial.println("Missing 'command' in payload.");
    Serial.println("Payload: ");
    for (unsigned int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    return;
  }

  String command = doc["command"].as<String>();

  if (commandHandlers.count(command))
  {
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    Serial.println("[Topic " + String(topic) + "] Received command: " + command + " with payload: " + jsonPayload);
    commandHandlers[command](doc);
  }
  else
  {
    Serial.println("Unknown command: " + command);
  }

  // String message;
  // for (unsigned int i = 0; i < length; i++) {
  //   message += (char)payload[i];
  // }
  // Serial.println("MQTT message received [" + String(topic) + "]: " + message);

  // mqttPublish(topics.igro.c_str(), readSoilMoisture().c_str());

  // Example: relay control via MQTT
  //   if (String(topic) == "irrigatore/relay") {
  //     if (message == "on") {
  //       digitalWrite(pin_Relay, LOW); // assume active LOW
  //     } else if (message == "off") {
  //       digitalWrite(pin_Relay, HIGH);
  //     }
  //   }
}
