#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>
#include <SPI.h>

class SDLogger
{
public:
    SDLogger();
    void begin();
    void logData(unsigned long time, int bat_nb, float volt, float current, bool switchState);
    bool shouldLog();
    void setLogTime(int time);

private:
    const int chipSelect = 10; // Pin de sélection de la carte SD
    File dataFile;
    unsigned long lastLogTime = 0;
    int log_at_time = 10; // Temps entre chaque enregistrement en secondes
};

#endif // SDLOGGER_H