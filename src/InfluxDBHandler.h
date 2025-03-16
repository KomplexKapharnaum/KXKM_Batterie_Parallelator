#ifndef INFLUXDBHANDLER_H
#define INFLUXDBHANDLER_H

#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
// InfluxDB Client
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

class InfluxDBHandler {
public:
    InfluxDBHandler(const char* serverUrl, const char* org, const char* bucket, const char* token, bool insecure);
    void begin();
    void writeBatteryData(const char* measurement, int batteryId, float voltage, float current, float temperature);
    void storeData(const char* measurement, const char* tags, const char* fields);
    void sendStoredData();

private:
    InfluxDBClient client;
    const char* serverUrl;
    const char* org;
    const char* bucket;
    const char* token;
};

#endif // INFLUXDBHANDLER_H
