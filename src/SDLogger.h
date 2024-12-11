
#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>
#include <SPI.h>

class SDLogger {
public:
    SDLogger();
    void begin();
    void logData(int deviceNumber, float data);

private:
    const int chipSelect = 10; // Pin de sélection de la carte SD
    File dataFile;
};

#endif // SDLOGGER_H