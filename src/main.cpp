const int I2C_Speed = 100; // I2C speed in KHz

// value for battery check
const float set_min_voltage = 24000; // Battery undervoltage threshold in mV
const float set_max_voltage = 30000; // Battery overvoltage threshold in mV
const int set_max_current = 1000;    // Battery overcurrent threshold in mA
const int set_max_charge_current =
    1000;                          // Battery charge current threshold in mA
const int reconnect_delay = 10000; // delay to reconnect the battery in ms
const int nb_switch_on =
    5; // number of switch on before infinite disconnect of the battery

// value for INA devices
const int max_INA_current = 50;       // Max current in A for INA devices
const int INA_micro_ohm_shunt = 2000; // Max micro ohm for INA devices

const bool print_message = true; // Print message on serial monitor

const int log_time =
    10; // Temps entre chaque enregistrement de log sur SD en secondes

#include "Batt_Parallelator_lib.h"
#include "SD_Logger.h"
#include "pin_mapppings.h"
#include <Arduino.h>

INAHandler inaHandler; // Créer une instance de la classe INAHandler
TCAHandler tcaHandler; // Créer une instance de la classe TCAHandler
BATTParallelator
    BattParallelator; // Créer une instance de la classe BattParallelator
SDLogger sdLogger;    // Créer une instance de la classe SDLogger
BatteryManager batteryManager; // Créer une instance de la classe BatteryManager

void I2C_scanner() { // Trouver tous les appareils I2C
  byte count = 0;
  for (byte i = 8; i < 120; i++) {
    Wire.beginTransmission(i);
    delay(50);
    if (Wire.endTransmission() == 0) {
      Serial.print("Adresse trouvée : ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);
      Serial.println(")");
      count++;
      delay(1); // peut-être inutile ?
    }           // fin de bonne réponse
  }             // fin de la boucle for
  Serial.println("Terminé.");
  Serial.print("Trouvé ");
  Serial.print(count, DEC);
  Serial.println(" appareil(s).");
} // fin de I2C_scanner

void setup() {
  if (print_message) {
    Serial.begin(115200);
    I2C_scanner();
  }
  Wire.begin(SDA_pin, SCL_pin);    // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // définir I2C

  tcaHandler.begin();
  Serial.println("Configuration TCA terminée");

  inaHandler.set_max_voltage(
      set_max_voltage); // définir la tension maximale en mV
  inaHandler.set_min_voltage(
      set_min_voltage); // définir la tension minimale en mV
  inaHandler.set_max_current(
      set_max_current); // définir le courant maximal en mA
  inaHandler.set_max_charge_current(
      set_max_charge_current); // définir le courant de charge maximal en mA
  inaHandler.begin(max_INA_current, INA_micro_ohm_shunt); // Initialiser les INA
  if (print_message)
    Serial.println("Configuration INA terminée");

  CSVConfig csvConfig = {
      ';'}; // Définir la configuration CSV avec un séparateur en point virgule
  sdLogger.begin("datalog", csvConfig); // Initialiser le logger SD avec le nom
                                        // du fichier et la configuration CSV
  sdLogger.setLogTime(
      log_time); // Définir le temps entre chaque enregistrement en secondes
  if (print_message)
    Serial.println("Configuration du logger SD terminée");

  BattParallelator.set_max_voltage(
      set_max_voltage); // définir la tension maximale en mV
  BattParallelator.set_min_voltage(
      set_min_voltage); // définir la tension minimale en mV
  BattParallelator.set_max_current(
      set_max_current); // définir le courant maximal en mA
  BattParallelator.set_max_charge_current(
      set_max_charge_current); // définir le courant de charge maximal en mA
  BattParallelator.set_reconnect_delay(
      reconnect_delay); // définir le délai de reconnexion en ms
  BattParallelator.set_nb_switch_on(
      nb_switch_on); // définir le nombre de commutations avant de déconnecter
                     // la batterie
  if (print_message)
    Serial.println("Configuration de la gestion des batteries terminée");

  int Nb_TCA = tcaHandler.getNbTCA(); // récupérer le nombre de TCA
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA
  if (print_message) {
    Serial.println();
    Serial.print("trouvé : ");
    Serial.print(Nb_TCA);
    Serial.print(" INA et ");
    Serial.print(Nb_INA);
    Serial.println(" TCA");
  }
  if (Nb_TCA !=
      Nb_INA / 4) // Vérifier si le nombre de TCA et de INA est correct
  {
    Serial.println("Erreur : Le nombre de TCA et de INA n'est pas correct");
    if (Nb_INA % 4 != 0) {
      Serial.println("Erreur : INA manquant");
    } else {
      Serial.println("Erreur : TCA manquant");
    }
  } else {
    Serial.println("Le nombre de TCA et de INA est correct");
  }

  // Démarrer la tâche de consommation en ampère-heure pour chaque batterie
  for (int i = 0; i < Nb_INA; i++) {
    batteryManager.startAmpereHourConsumptionTask(i, 1.0,
                                                  600); // 600 mesures par heure
  }
}

void loop() {
  int Nb_Batt = inaHandler.getNbINA(); // récupérer le nombre de INA
  float battery_voltages[Nb_Batt];
  for (int i = 0; i < Nb_Batt; i++) // loop through all INA devices
  {
    if (print_message) // read the INA device to serial monitor
      inaHandler.read(i, print_message);
    BattParallelator.check_battery_connected_status(
        i); // check battery status and switch on/off if needed
    BattParallelator.find_max_voltage(battery_voltages,
                                      i); // Trouver la tension maximale
    if (sdLogger.shouldLog()) // Vérifier si on doit enregistrer les données
      sdLogger.logData(millis(), i, inaHandler.read_volt(i),
                       inaHandler.read_current(i),
                       BattParallelator.check_battery_status(i),
                       batteryManager.getAmpereHourConsumption(
                           i)); // Enregistrer les données sur la carte SD
  }                             // of for-next loop through all INA devices
  delay(500);
} // of loop