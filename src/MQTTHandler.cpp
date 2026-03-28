#include "MQTTHandler.h"

#include <DebugLogger.h>

extern DebugLogger debugLogger;

MQTTHandler::MQTTHandler(const char *brokerHost, uint16_t brokerPort,
                         const char *clientId, const char *username,
                         const char *password)
    : client(wifiClient), brokerHost(brokerHost), brokerPort(brokerPort),
      clientId(clientId), username(username), password(password),
      lastReconnectAttemptMs(0) {}

void MQTTHandler::begin() {
  client.setServer(brokerHost, brokerPort);
}

bool MQTTHandler::isConnected() const {
  return const_cast<PubSubClient&>(client).connected();
}

bool MQTTHandler::ensureConnected() {
  if (client.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - lastReconnectAttemptMs) < 5000) {
    return false;
  }
  lastReconnectAttemptMs = nowMs;

  const bool ok = (username != nullptr && username[0] != '\0')
                      ? client.connect(clientId, username, password)
                      : client.connect(clientId);

  if (!ok) {
    debugLogger.println(DebugLogger::WARNING,
                        "MQTT reconnect failed rc=" +
                            String(static_cast<int>(client.state())));
    return false;
  }

  debugLogger.println(DebugLogger::INFO, "MQTT connected");
  return true;
}

void MQTTHandler::loop() {
  if (ensureConnected()) {
    client.loop();
  }
}

bool MQTTHandler::publishJson(const char *topic, const String &payload,
                              bool retained) {
  if (topic == nullptr || topic[0] == '\0' || payload.length() == 0) {
    return false;
  }

  if (!ensureConnected()) {
    return false;
  }

  return client.publish(topic, payload.c_str(), retained);
}
