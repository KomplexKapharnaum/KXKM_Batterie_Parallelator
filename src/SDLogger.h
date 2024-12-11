#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>
#include <SPI.h>

class SDLogger
{
public:
    SDLogger();
    void begin();
    void logData(float volt, float current, bool switchState, unsigned long time);
    bool shouldLog();

private:
    const int chipSelect = 10; // Pin de sélection de la carte SD
    File dataFile;
    unsigned long lastLogTime = 0;
};

#endif // SDLOGGER_H