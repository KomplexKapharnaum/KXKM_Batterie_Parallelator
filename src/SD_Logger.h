#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>
#include <SPI.h>

class SDLogger
{
public:
    SDLogger();
    /**
     * @brief Initialize the SD card and open the log file.
     */
    void begin();
    
    /**
     * @brief Log data to the SD card.
     * 
     * @param time The current time.
     * @param bat_nb The battery number.
     * @param volt The voltage.
     * @param current The current.
     * @param switchState The state of the switch.
     */
    void logData(unsigned long time, int bat_nb, float volt, float current, bool switchState);
    
    /**
     * @brief Check if it's time to log data.
     * 
     * @return true if it's time to log data, false otherwise.
     */
    bool shouldLog();
    
    /**
     * @brief Set the logging interval time.
     * 
     * @param time The logging interval time in seconds.
     */
    void setLogTime(int time);

private:
    const int chipSelect = 10; // Pin de sélection de la carte SD
    File dataFile;
    unsigned long lastLogTime = 0;
    int log_at_time = 10; // Temps entre chaque enregistrement en secondes
};

#endif // SDLOGGER_H