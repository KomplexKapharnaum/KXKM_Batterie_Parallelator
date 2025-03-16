#include "InfluxDBHandler.h"
#include <DebugLogger.h>
#include <FS.h>
#include <SPI.h>
#include <SPIFFS.h>
//#include <ESP_SSLClient.h>
#include <WiFiClientSecure.h> // Ajoutez cette ligne pour utiliser WiFiClientSecure

#define WRITE_PRECISION WritePrecision::S
// Define the InfluxDB client batch size. This is the maximum number of points
// that will be sent to the server in a single call.
#define MAX_BATCH_SIZE 20
// Define the InfluxDB client buffer size. This is the maximum number of points
// that will be stored in memory before sending them to the server. If the
// buffer is full and new points are added, old points are dropped. the buffer
// size must be at least as large as the batch size. The buffer size need RAM
// !!!
// TODO : USE SD CARD TO STORE DATA and send in the buffer
#define WRITE_BUFFER_SIZE 50

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;

InfluxDBHandler::InfluxDBHandler(const char *serverUrl, const char *org,
                                 const char *bucket, const char *token,
                                 bool insecure)
    : serverUrl(serverUrl), org(org), bucket(bucket), token(token),
      client(serverUrl, org, bucket, token, InfluxDbCloud2CACert) {
 // if (insecure) {
    client.setInsecure();
 // }
  client.setWriteOptions(WriteOptions()
                             .writePrecision(WRITE_PRECISION)
                             .batchSize(4)
                             .bufferSize(20)
                             .flushInterval(10)
                             .retryInterval(5)
                             .maxRetryAttempts(5));
  client.setHTTPOptions(
      HTTPOptions().connectionReuse(true).httpReadTimeout(2000));
    }

void InfluxDBHandler::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.printlnDebug(DebugLogger::WIFI, "WiFi not connected. Cannot connect to InfluxDB.");
        return;
    }
    if (!client.validateConnection()) {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "InfluxDB connection failed: " + client.getLastErrorMessage());
    } else {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Connected to InfluxDB");
    }
}

void InfluxDBHandler::writeBatteryData(const char *measurement, int batteryId,
                                       float voltage, float current,
                                       float temperature) {
    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.printlnDebug(DebugLogger::WIFI, "WiFi not connected. Cannot write to InfluxDB.");
        storeData(measurement, ("battery=" + String(batteryId)).c_str(), ("voltage=" + String(voltage) + ",current=" + String(current) + ",temperature=" + String(temperature)).c_str());
        return;
    }
    if (!client.validateConnection()) {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "InfluxDB connection failed: " + client.getLastErrorMessage());
        storeData(measurement, ("battery=" + String(batteryId)).c_str(), ("voltage=" + String(voltage) + ",current=" + String(current) + ",temperature=" + String(temperature)).c_str());
        return;
    }

    Point point(measurement);
    point.addTag(
        "battery",
        String(batteryId).c_str()); // Ajoutez un tag pour identifier la batterie
    point.addField("voltage", voltage);
    point.addField("current", current);
    point.addField("temperature", temperature);
    if (!client.writePoint(point)) {
        String errorMessage = client.getLastErrorMessage();
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "InfluxDB write failed: " + errorMessage);
        if (errorMessage.indexOf("SSL - Internal error") != -1) {
            debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Attempting to re-establish connection...");
        }
        storeData(measurement, ("battery=" + String(batteryId)).c_str(), ("voltage=" + String(voltage) + ",current=" + String(current) + ",temperature=" + String(temperature)).c_str());
    } else {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Data written to InfluxDB");
    }
}

void InfluxDBHandler::storeData(const char *measurement, const char *tags,
                                const char *fields) {
    const String Filename = String("/influxdb_temp.txt");

    if (ESP.getFreeHeap() < 5000) {
        debugLogger.printlnDebug(DebugLogger::ERROR, "Pas assez de mémoire pour initialiser SPIFFS !");
        return;
    }

    if (!SPIFFS.begin(true)) {
        debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'initialisation de SPIFFS !");
        return;
    }

    if (!SPIFFS.exists(Filename.c_str())) {
        File file = SPIFFS.open(Filename.c_str(), FILE_WRITE);
        if (!file) {
            debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Failed to create file");
            return;
        }
        file.close();
    }

    File file = SPIFFS.open(Filename.c_str(), FILE_APPEND);
    if (file) {
        file.print(measurement);
        file.print(",");
        file.print(tags);
        file.print(",");
        file.println(fields);
        file.close();
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Data stored temporarily on SPIFFS");
    } else {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Failed to open file for writing");
    }
}

void InfluxDBHandler::sendStoredData() {
    if (ESP.getFreeHeap() < 5000) {
        debugLogger.printlnDebug(DebugLogger::ERROR, "Pas assez de mémoire pour initialiser SPIFFS !");
        return;
    }

    if (!SPIFFS.begin(true)) {
        debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'initialisation de SPIFFS !");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.printlnDebug(DebugLogger::WIFI, "WiFi not connected. Cannot send stored data to InfluxDB.");
        return;
    }
    if (!client.validateConnection()) {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "InfluxDB connection failed: " + client.getLastErrorMessage());
        return;
    }

    const String Filename = String("/influxdb_temp.txt");

    if (!SPIFFS.begin(true)) {
        debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'initialisation de SPIFFS !");
        return;
    }

    if (!SPIFFS.exists(Filename.c_str())) {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "No stored data to send");
        return;
    }

    File file = SPIFFS.open(Filename.c_str(), FILE_READ);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            int firstComma = line.indexOf(',');
            int secondComma = line.indexOf(',', firstComma + 1);
            String measurement = line.substring(0, firstComma);
            String tags = line.substring(firstComma + 1, secondComma);
            String fields = line.substring(secondComma + 1);

            Point point(measurement.c_str());
            point.addTag("battery", tags.c_str());

            int fieldStart = 0;
            int fieldEnd = fields.indexOf(',');
            while (fieldEnd != -1) {
                String field = fields.substring(fieldStart, fieldEnd);
                int equalSign = field.indexOf('=');
                if (equalSign == -1) {
                    debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Malformed field: " + field);
                    continue;
                }
                String fieldName = field.substring(0, equalSign);
                String fieldValue = field.substring(equalSign + 1);
                point.addField(fieldName.c_str(), fieldValue.toFloat());
                fieldStart = fieldEnd + 1;
                fieldEnd = fields.indexOf(',', fieldStart);
            }

            String field = fields.substring(fieldStart);
            int equalSign = field.indexOf('=');
            if (equalSign == -1) {
                debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Malformed field: " + field);
            } else {
                String fieldName = field.substring(0, equalSign);
                String fieldValue = field.substring(equalSign + 1);
                point.addField(fieldName.c_str(), fieldValue.toFloat());
            }

            if (!client.writePoint(point)) {
                debugLogger.printlnDebug(DebugLogger::INFLUXDB, "InfluxDB write failed: " + client.getLastErrorMessage());
                file.close();
                return;
            }
        }
        file.close();
        SPIFFS.remove("/influxdb_temp.txt");
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Stored data sent to InfluxDB");
    } else {
        debugLogger.printlnDebug(DebugLogger::INFLUXDB, "Failed to open file for reading");
    }
}
