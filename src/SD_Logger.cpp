#include "SD_Logger.h"

SDLogger::SDLogger() {}

void SDLogger::begin()
{
    if (!SD.begin(chipSelect))
    {
        Serial.println("Initialization of SD card failed!");
        return;
    }
    dataFile = SD.open("datalog.csv", FILE_WRITE);
    dataFile.println("Time,Battery Number,Voltage,Current,Switch State");
    if (!dataFile)
    {
        Serial.println("Failed to open datalog.csv for writing!");
    }
}

void SDLogger::logData(unsigned long time, int bat_nb, float volt, float current, bool switchState)
{
    if (dataFile)
    {
        dataFile.print(time);
        dataFile.print(",");
        dataFile.print(bat_nb);
        dataFile.print(",");
        dataFile.print(volt);
        dataFile.print(",");
        dataFile.print(current);
        dataFile.print(",");
        dataFile.println(switchState ? "ON" : "OFF");
        dataFile.flush();
    }
    else
    {
        Serial.println("Error writing to datalog.csv");
    }
}

bool SDLogger::shouldLog()
{
    unsigned long currentTime = millis();
    if (currentTime - lastLogTime >= log_at_time)
    { // 10 secondes
        lastLogTime = currentTime;
        return true;
    }
    return false;
}

void SDLogger::setLogTime(int time) // set log time in seconds
{
    log_at_time = time * 1000;
}
