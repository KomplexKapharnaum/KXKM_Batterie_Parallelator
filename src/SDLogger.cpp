
#include "SDLogger.h"

SDLogger::SDLogger() {}

void SDLogger::begin() {
    if (!SD.begin(chipSelect)) {
        Serial.println("Initialization of SD card failed!");
        return;
    }
    dataFile = SD.open("datalog.txt", FILE_WRITE);
    if (!dataFile) {
        Serial.println("Failed to open datalog.txt for writing!");
    }
}

void SDLogger::logData(int deviceNumber, float data) {
    if (dataFile) {
        dataFile.print("Device ");
        dataFile.print(deviceNumber);
        dataFile.print(": ");
        dataFile.println(data);
        dataFile.flush();
    } else {
        Serial.println("Error writing to datalog.txt");
    }
}