#ifndef INFLUXDBHANDLER_H
#define INFLUXDBHANDLER_H

#ifdef __cplusplus
class String;
class InfluxDBClient;

class InfluxDBHandler {
public:
    InfluxDBHandler(const char* serverUrl, const char* org, const char* bucket, const char* token, bool insecure);
    ~InfluxDBHandler();
    void begin();
    void writeBatteryData(const char* measurement, int batteryId, float voltage,
                          float current, float ampereHourConsumption,
                          float ampereHourCharge, float totalConsumption,
                          float totalCharge, float totalCurrent);
    void writeAggregatedBatteryData(const char* measurement,
                                    const char* profileTag,
                                    const String& fields);
    void storeData(const char* measurement, const char* tags, const char* fields);
    void sendStoredData();

private:
    InfluxDBClient* client;
    const char* serverUrl;
    const char* org;
    const char* bucket;
    const char* token;
};
#endif

#endif // INFLUXDBHANDLER_H
