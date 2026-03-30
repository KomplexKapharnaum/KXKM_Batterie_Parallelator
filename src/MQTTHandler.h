#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

class MQTTHandler {
public:
  MQTTHandler(const char *brokerHost, uint16_t brokerPort,
              const char *clientId, const char *username,
              const char *password);

  void begin();
  void loop();
  bool publishJson(const char *topic, const String &payload, bool retained = false);
  bool isConnected() const;

private:
  bool ensureConnected();

  WiFiClient wifiClient;
  PubSubClient client;
  const char *brokerHost;
  uint16_t brokerPort;
  const char *clientId;
  const char *username;
  const char *password;
  uint32_t lastReconnectAttemptMs;
};

#endif // MQTT_HANDLER_H
