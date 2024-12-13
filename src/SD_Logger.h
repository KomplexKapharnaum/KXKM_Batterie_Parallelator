#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>
#include <SPI.h>

struct CSVConfig {
  char separator;
};

class SDLogger {
public:
  SDLogger();
  /**
   * @brief Initialiser la carte SD et ouvrir le fichier de log.
   * @param filename Le nom du fichier de log.
   * @param config La configuration du CSV.
   */
  void begin(const char *filename, CSVConfig config);

  /**
   * @brief Enregistrer les données sur la carte SD.
   *
   * @param time Le temps actuel.
   * @param bat_nb Le numéro de la batterie.
   * @param volt La tension.
   * @param current Le courant.
   * @param switchState L'état du commutateur.
   * @param ampereHour La consommation en ampère-heure.
   */
  void logData(unsigned long time, int bat_nb, float volt, float current,
               bool switchState, float ampereHour);

  /**
   * @brief Vérifier s'il est temps d'enregistrer les données.
   *
   * @return true s'il est temps d'enregistrer les données, false sinon.
   */
  bool shouldLog();

  /**
   * @brief Définir l'intervalle de temps d'enregistrement.
   *
   * @param time L'intervalle de temps d'enregistrement en secondes.
   */
  void setLogTime(int time);

private:
  const int chipSelect = 10; // Pin de sélection de la carte SD
  File dataFile;
  unsigned long lastLogTime = 0;
  int log_at_time = 10; // Temps entre chaque enregistrement en secondes
  CSVConfig csvConfig;  // Ajouter cette ligne
};

#endif // SDLOGGER_H