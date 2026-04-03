#include "InfluxDBHandler.h"
#include "InfluxBufferCodec.h"
#if __has_include(<InfluxDbClient.h>)
#define KXKM_HAS_INFLUX_LIB 1
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#else
#define KXKM_HAS_INFLUX_LIB 0
#endif
#if __has_include(<KxLogger.h>)
#include <KxLogger.h>
#elif __has_include("../lib/KxLogger/src/KxLogger.h")
#include "../lib/KxLogger/src/KxLogger.h"
#endif
#include <FS.h>
#include <SPI.h>
#include <SPIFFS.h>
//#include <ESP_SSLClient.h>
#include <WiFiClientSecure.h> // Ajoutez cette ligne pour utiliser WiFiClientSecure
#include <cstdlib>
#include <string>

#if KXKM_HAS_INFLUX_LIB

namespace {
const char *kInfluxTempFile = "/influxdb_temp.txt";
const char *kInfluxTempFileRotated = "/influxdb_temp_prev.txt";
// Keep the buffered file bounded to reduce SPIFFS pressure in long offline periods.
static constexpr size_t kInfluxTempFileMaxBytes = 32 * 1024;
static constexpr size_t kInfluxMaxBufferedLineLen = 512;

bool rotateInfluxTempIfNeeded() {
    if (!SPIFFS.exists(kInfluxTempFile)) {
        return true;
    }

    File current = SPIFFS.open(kInfluxTempFile, FILE_READ);
    if (!current) {
        return false;
    }
    const size_t sz = current.size();
    current.close();

    if (sz < kInfluxTempFileMaxBytes) {
        return true;
    }

    if (SPIFFS.exists(kInfluxTempFileRotated)) {
        SPIFFS.remove(kInfluxTempFileRotated);
    }

    if (!SPIFFS.rename(kInfluxTempFile, kInfluxTempFileRotated)) {
        return false;
    }

    File recreated = SPIFFS.open(kInfluxTempFile, FILE_WRITE);
    if (!recreated) {
        return false;
    }
    recreated.close();
    return true;
}

bool replayStoredFile(const char *filePath, InfluxDBClient &clientRef) {
    if (!SPIFFS.exists(filePath)) {
        return true;
    }

    File file = SPIFFS.open(filePath, FILE_READ);
    if (!file) {
        return false;
    }

    while (file.available()) {
        String lineStr = file.readStringUntil('\n');
        lineStr.trim();
        if (lineStr.length() == 0) {
            continue;
        }

        ParsedInfluxBufferLine parsed;
        if (!parseInfluxBufferLine(std::string(lineStr.c_str()), parsed)) {
            continue;
        }

        Point point(parsed.measurement.c_str());
        point.addTag(parsed.tag.key.c_str(), parsed.tag.value.c_str());
        for (size_t i = 0; i < parsed.fields.size(); ++i) {
            point.addField(parsed.fields[i].key.c_str(),
                           static_cast<float>(std::atof(parsed.fields[i].value.c_str())));
        }

        if (!clientRef.writePoint(point)) {
            file.close();
            return false;
        }
    }

    file.close();
    SPIFFS.remove(filePath);
    return true;
}

bool appendBufferedLine(const char *filePath, const String &line) {
    File file = SPIFFS.open(filePath, FILE_APPEND);
    if (!file) {
        return false;
    }
    file.println(line);
    file.close();
    return true;
}
} // namespace

#define WRITE_PRECISION WritePrecision::S
// Define the InfluxDB client batch size. This is the maximum number of points
// that will be sent to the server in a single call.
#define MAX_BATCH_SIZE 20
// Define the InfluxDB client buffer size. This is the maximum number of points
// that will be stored in memory before sending them to the server. If the
// buffer is full and new points are added, old points are dropped. the buffer
// size must be at least as large as the batch size. The buffer size need RAM
// !!!
#define WRITE_BUFFER_SIZE 50

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern KxLogger debugLogger;

InfluxDBHandler::InfluxDBHandler(const char *serverUrl, const char *org,
                                 const char *bucket, const char *token,
                                 bool insecure)
        : client(new InfluxDBClient(serverUrl, org, bucket, token,
                                                                InfluxDbCloud2CACert)),
            serverUrl(serverUrl), org(org), bucket(bucket), token(token) {
    // TASK-005: TLS enforcement safeguard — never disable TLS for production cloud communication.
    // Even if insecure flag is passed, TLS verification is enforced.
    // Cloud telemetry contains battery state, switch history, and operational secrets.
    if (insecure) {
        debugLogger.println(KxLogger::WARNING,
                            "InfluxDB insecure mode requested but REJECTED — TLS mandatory");
        // Do NOT call setInsecure() — TLS verification stays active
    }
    // TLS is always required for cloud endpoints
    client->setWriteOptions(WriteOptions()
                             .writePrecision(WRITE_PRECISION)
                             .batchSize(4)
                             .bufferSize(20)
                             .flushInterval(10)
                             .retryInterval(5)
                             .maxRetryAttempts(5));
    client->setHTTPOptions(
      HTTPOptions().connectionReuse(true).httpReadTimeout(2000));
    }

InfluxDBHandler::~InfluxDBHandler() {
        delete client;
        client = nullptr;
}

void InfluxDBHandler::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.println(KxLogger::WIFI, "WiFi not connected. Cannot connect to InfluxDB.");
        return;
    }
    if (!client->validateConnection()) {
        debugLogger.println(KxLogger::INFLUXDB, "InfluxDB connection failed: " + client->getLastErrorMessage());
    } else {
        debugLogger.println(KxLogger::INFLUXDB, "Connected to InfluxDB");
    }
}

void InfluxDBHandler::writeBatteryData(const char *measurement, int batteryId,
                                       float voltage, float current,
                                       float ampereHourConsumption,
                                       float ampereHourCharge,
                                       float totalConsumption,
                                       float totalCharge,
                                       float totalCurrent) {
    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.println(KxLogger::WIFI, "WiFi not connected. Cannot write to InfluxDB.");
        storeData(measurement, ("battery=" + String(batteryId)).c_str(),
                  ("voltage=" + String(voltage, 6) +
                   ",current=" + String(current, 6) +
                   ",ah_consumption=" + String(ampereHourConsumption, 6) +
                   ",ah_charge=" + String(ampereHourCharge, 6) +
                   ",total_consumption=" + String(totalConsumption, 6) +
                   ",total_charge=" + String(totalCharge, 6) +
                   ",total_current=" + String(totalCurrent, 6))
                      .c_str());
        return;
    }
    if (!client->validateConnection()) {
        debugLogger.println(KxLogger::INFLUXDB, "InfluxDB connection failed: " + client->getLastErrorMessage());
        storeData(measurement, ("battery=" + String(batteryId)).c_str(),
                  ("voltage=" + String(voltage, 6) +
                   ",current=" + String(current, 6) +
                   ",ah_consumption=" + String(ampereHourConsumption, 6) +
                   ",ah_charge=" + String(ampereHourCharge, 6) +
                   ",total_consumption=" + String(totalConsumption, 6) +
                   ",total_charge=" + String(totalCharge, 6) +
                   ",total_current=" + String(totalCurrent, 6))
                      .c_str());
        return;
    }

    Point point(measurement);
    point.addTag(
        "battery",
        String(batteryId).c_str()); // Ajoutez un tag pour identifier la batterie
    point.addField("voltage", voltage);
    point.addField("current", current);
    point.addField("ah_consumption", ampereHourConsumption);
    point.addField("ah_charge", ampereHourCharge);
    point.addField("total_consumption", totalConsumption);
    point.addField("total_charge", totalCharge);
    point.addField("total_current", totalCurrent);
    if (!client->writePoint(point)) {
        String errorMessage = client->getLastErrorMessage();
        debugLogger.println(KxLogger::INFLUXDB, "InfluxDB write failed: " + errorMessage);
        if (errorMessage.indexOf("SSL - Internal error") != -1) {
            debugLogger.println(KxLogger::INFLUXDB, "Attempting to re-establish connection...");
        }
        storeData(measurement, ("battery=" + String(batteryId)).c_str(),
                  ("voltage=" + String(voltage, 6) +
                   ",current=" + String(current, 6) +
                   ",ah_consumption=" + String(ampereHourConsumption, 6) +
                   ",ah_charge=" + String(ampereHourCharge, 6) +
                   ",total_consumption=" + String(totalConsumption, 6) +
                   ",total_charge=" + String(totalCharge, 6) +
                   ",total_current=" + String(totalCurrent, 6))
                      .c_str());
    } else {
        debugLogger.println(KxLogger::INFLUXDB, "Data written to InfluxDB");
    }
}

void InfluxDBHandler::writeAggregatedBatteryData(const char *measurement,
                                                 const char *profileTag,
                                                 const String &fields) {
    if (fields.length() == 0) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.println(KxLogger::WIFI,
                            "WiFi not connected. Aggregated data buffered.");
        storeData(measurement,
                  ("profile=" + String(profileTag == nullptr ? "normal"
                                                              : profileTag))
                      .c_str(),
                  fields.c_str());
        return;
    }

    if (!client->validateConnection()) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "InfluxDB connection failed: " +
                                client->getLastErrorMessage());
        storeData(measurement,
                  ("profile=" + String(profileTag == nullptr ? "normal"
                                                              : profileTag))
                      .c_str(),
                  fields.c_str());
        return;
    }

    Point point(measurement);
    point.addTag("profile",
                 (profileTag == nullptr) ? "normal" : profileTag);

    ParsedInfluxBufferLine parsed;
    if (!parseInfluxBufferLine(std::string((String(measurement) + ",profile=" +
                                           String((profileTag == nullptr)
                                                      ? "normal"
                                                      : profileTag) +
                                           "," + fields)
                                              .c_str()),
                              parsed)) {
        storeData(measurement,
                  ("profile=" + String(profileTag == nullptr ? "normal"
                                                              : profileTag))
                      .c_str(),
                  fields.c_str());
        return;
    }

    for (size_t i = 0; i < parsed.fields.size(); ++i) {
        const String key = parsed.fields[i].key.c_str();
        const String rawValue = parsed.fields[i].value.c_str();
        if (rawValue.endsWith("i")) {
            const String intPart = rawValue.substring(0, rawValue.length() - 1);
            point.addField(key.c_str(), intPart.toInt());
        } else {
            point.addField(key.c_str(), static_cast<float>(std::atof(rawValue.c_str())));
        }
    }

    if (!client->writePoint(point)) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "InfluxDB aggregated write failed: " +
                                client->getLastErrorMessage());
        storeData(measurement,
                  ("profile=" + String(profileTag == nullptr ? "normal"
                                                              : profileTag))
                      .c_str(),
                  fields.c_str());
        return;
    }

    debugLogger.println(KxLogger::INFLUXDB,
                        "Aggregated data written to InfluxDB");
}

void InfluxDBHandler::storeData(const char *measurement, const char *tags,
                                const char *fields) {
    if (measurement == nullptr || tags == nullptr || fields == nullptr) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "storeData refused: null input segment");
        return;
    }

    const String line = String(measurement) + "," + String(tags) + "," +
                        String(fields);
    if (line.length() == 0 || line.length() > kInfluxMaxBufferedLineLen) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "storeData refused: invalid line length");
        return;
    }

    ParsedInfluxBufferLine parsed;
    if (!parseInfluxBufferLine(std::string(line.c_str()), parsed)) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "storeData refused: malformed line");
        return;
    }

    const String Filename = String(kInfluxTempFile);

    if (ESP.getFreeHeap() < 5000) {
        debugLogger.println(KxLogger::ERROR, "Pas assez de mémoire pour initialiser SPIFFS !");
        return;
    }

    if (!SPIFFS.begin(true)) {
        debugLogger.println(KxLogger::ERROR, "Échec de l'initialisation de SPIFFS !");
        return;
    }

    if (!SPIFFS.exists(Filename.c_str())) {
        File file = SPIFFS.open(Filename.c_str(), FILE_WRITE);
        if (!file) {
            debugLogger.println(KxLogger::INFLUXDB, "Failed to create file");
            return;
        }
        file.close();
    }

    if (!rotateInfluxTempIfNeeded()) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "Failed to rotate Influx temp file");
    }

    if (appendBufferedLine(Filename.c_str(), line)) {
        debugLogger.println(KxLogger::INFLUXDB, "Data stored temporarily on SPIFFS");
    } else {
        if (appendBufferedLine(kInfluxTempFileRotated, line)) {
            debugLogger.println(KxLogger::INFLUXDB,
                                "Primary file unavailable, data buffered in rotated file");
        } else {
            debugLogger.println(KxLogger::INFLUXDB,
                                "Failed to persist buffered data in SPIFFS");
        }
    }
}

void InfluxDBHandler::sendStoredData() {
    if (ESP.getFreeHeap() < 5000) {
        debugLogger.println(KxLogger::ERROR, "Pas assez de mémoire pour initialiser SPIFFS !");
        return;
    }

    if (!SPIFFS.begin(true)) {
        debugLogger.println(KxLogger::ERROR, "Échec de l'initialisation de SPIFFS !");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        debugLogger.println(KxLogger::WIFI, "WiFi not connected. Cannot send stored data to InfluxDB.");
        return;
    }
    if (!client->validateConnection()) {
        debugLogger.println(KxLogger::INFLUXDB, "InfluxDB connection failed: " + client->getLastErrorMessage());
        return;
    }

    const bool hasCurrent = SPIFFS.exists(kInfluxTempFile);
    const bool hasRotated = SPIFFS.exists(kInfluxTempFileRotated);
    if (!hasCurrent && !hasRotated) {
        debugLogger.println(KxLogger::INFLUXDB, "No stored data to send");
        return;
    }

    if (!replayStoredFile(kInfluxTempFileRotated, *client)) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "InfluxDB replay failed on rotated file: " +
                                client->getLastErrorMessage());
        return;
    }

    if (!replayStoredFile(kInfluxTempFile, *client)) {
        debugLogger.println(KxLogger::INFLUXDB,
                            "InfluxDB replay failed on current file: " +
                                client->getLastErrorMessage());
        return;
    }

    debugLogger.println(KxLogger::INFLUXDB, "Stored data sent to InfluxDB");
}

#else

extern KxLogger debugLogger;

InfluxDBHandler::InfluxDBHandler(const char *serverUrl, const char *org,
                                 const char *bucket, const char *token,
                                 bool insecure)
    : client(nullptr), serverUrl(serverUrl), org(org), bucket(bucket),
      token(token) {
    (void)insecure;
}

InfluxDBHandler::~InfluxDBHandler() {}

void InfluxDBHandler::begin() {
    debugLogger.println(KxLogger::WARNING,
                        "InfluxDB lib unavailable in this analysis context");
}

void InfluxDBHandler::writeBatteryData(const char *measurement, int batteryId,
                                       float voltage, float current,
                                       float ampereHourConsumption,
                                       float ampereHourCharge,
                                       float totalConsumption,
                                       float totalCharge,
                                       float totalCurrent) {
    (void)measurement;
    (void)batteryId;
    (void)voltage;
    (void)current;
    (void)ampereHourConsumption;
    (void)ampereHourCharge;
    (void)totalConsumption;
    (void)totalCharge;
    (void)totalCurrent;
}

void InfluxDBHandler::writeAggregatedBatteryData(const char *measurement,
                                                 const char *profileTag,
                                                 const String &fields) {
    (void)measurement;
    (void)profileTag;
    (void)fields;
}

void InfluxDBHandler::storeData(const char *measurement, const char *tags,
                                const char *fields) {
    (void)measurement;
    (void)tags;
    (void)fields;
}

void InfluxDBHandler::sendStoredData() {}

#endif
