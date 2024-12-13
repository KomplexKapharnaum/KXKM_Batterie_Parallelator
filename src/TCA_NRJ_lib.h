#ifndef TCA_NRJ_LIB_H
#define TCA_NRJ_LIB_H

#include "TCA9555.h" // Bibliothèque TCA9555

class TCAHandler {
public:
  TCAHandler();
  void begin();
  bool read(int TCA_num, int pin);
  bool write(int TCA_num, int pin, bool value);
  void check_INA_TCA_address();
  uint8_t getNbTCA();
  byte getDeviceAddress(int TCA_num);

private:
  void initialize_tca(TCA9535 &tca, const char *name);
  const byte TCA_address[8] = {
      0x20, 0x21, 0x22, 0x23,
      0x24, 0x25, 0x26, 0x27}; // Tableau des adresses TCA9535
  TCA9535 TCA_0, TCA_1, TCA_2, TCA_3, TCA_4, TCA_5, TCA_6, TCA_7;
  int Nb_TCA;
  byte TCA_address_connected[8];
};

/*! @brief Constructeur pour TCAHandler
 */
TCAHandler::TCAHandler()
    : TCA_0(TCA_address[0]), TCA_1(TCA_address[1]), TCA_2(TCA_address[2]),
      TCA_3(TCA_address[3]), TCA_4(TCA_address[4]), TCA_5(TCA_address[5]),
      TCA_6(TCA_address[6]), TCA_7(TCA_address[7]),
      Nb_TCA(0) { // Initialisation de Nb_TCA
  memset(TCA_address_connected, 0, sizeof(TCA_address_connected));
}

/*! @brief Initialiser un appareil TCA
    @param tca L'appareil TCA
    @param name Le nom de l'appareil TCA
*/
void TCAHandler::initialize_tca(TCA9535 &tca, const char *name) {
  tca.begin();
  Serial.println(String(name) + " est connecté");
  for (int i = 0; i < 4; i++) {
    tca.pinMode1(i, OUTPUT);
    tca.write1(i, LOW);
  }
  for (int i = 5; i < 7; i++) {
    tca.pinMode1(i, INPUT);
  }
  for (int i = 8; i < 16; i++) {
    tca.pinMode1(i, OUTPUT);
    tca.write1(i, LOW);
  }
}

/*! @brief Démarrer le TCAHandler et initialiser les appareils TCA connectés
 */
void TCAHandler::begin() {
  Serial.println();
  for (int i = 0; i < 8; i++) {
    Wire.beginTransmission(TCA_address[i]);
    delay(50);
    if (Wire.endTransmission() == 0) {
      Nb_TCA++;
      TCA_address_connected[Nb_TCA - 1] = TCA_address[i];

      switch (i) {
      case 0:
        initialize_tca(TCA_0, "TCA_0");
        break;
      case 1:
        initialize_tca(TCA_1, "TCA_1");
        break;
      case 2:
        initialize_tca(TCA_2, "TCA_2");
        break;
      case 3:
        initialize_tca(TCA_3, "TCA_3");
        break;
      case 4:
        initialize_tca(TCA_4, "TCA_4");
        break;
      case 5:
        initialize_tca(TCA_5, "TCA_5");
        break;
      case 6:
        initialize_tca(TCA_6, "TCA_6");
        break;
      case 7:
        initialize_tca(TCA_7, "TCA_7");
        break;
      }
    }
  }
  Serial.print("trouvé ");
  Serial.print(Nb_TCA);
  Serial.println(" appareils");
}

/*! @brief Lire la valeur d'une broche sur un appareil TCA
    @param TCA_num Le numéro de l'appareil TCA
    @param pin Le numéro de la broche
    @return La valeur de la broche
*/
bool TCAHandler::read(int TCA_num, int pin) {
  bool val;
  switch (TCA_num) {
  case 0:
    val = TCA_0.read1(pin);
    break;
  case 1:
    val = TCA_1.read1(pin);
    break;
  case 2:
    val = TCA_2.read1(pin);
    break;
  case 3:
    val = TCA_3.read1(pin);
    break;
  case 4:
    val = TCA_4.read1(pin);
    break;
  case 5:
    val = TCA_5.read1(pin);
    break;
  case 6:
    val = TCA_6.read1(pin);
    break;
  case 7:
    val = TCA_7.read1(pin);
    break;
  default:
    return false;
  }
  Serial.print(val);
  Serial.print('\t');
  return val;
}

/*! @brief Écrire une valeur sur une broche d'un appareil TCA
    @param TCA_num Le numéro de l'appareil TCA
    @param pin Le numéro de la broche
    @param value La valeur à écrire
    @return True si l'écriture a réussi, false sinon
*/
bool TCAHandler::write(int TCA_num, int pin, bool value) {
  switch (TCA_num) {
  case 0:
    TCA_0.write1(pin, value);
    break;
  case 1:
    TCA_1.write1(pin, value);
    break;
  case 2:
    TCA_2.write1(pin, value);
    break;
  case 3:
    TCA_3.write1(pin, value);
    break;
  case 4:
    TCA_4.write1(pin, value);
    break;
  case 5:
    TCA_5.write1(pin, value);
    break;
  case 6:
    TCA_6.write1(pin, value);
    break;
  case 7:
    TCA_7.write1(pin, value);
    break;
  default:
    return false;
  }
  return true;
}

/*! @brief Obtenir l'adresse de l'appareil TCA
    @param TCA_num Le numéro de l'appareil TCA
    @return L'adresse de l'appareil
*/
byte TCAHandler::getDeviceAddress(int TCA_num) { return TCA_address[TCA_num]; }

/*! @brief Obtenir le nombre d'appareils TCA trouvés
    @return Le nombre d'appareils TCA
*/
uint8_t TCAHandler::getNbTCA() {
  uint8_t Nb_TCA = 0;
  for (int i = 0; i < 8; i++) {
    Wire.beginTransmission(TCA_address[i]);
    delay(50);
    if (Wire.endTransmission() == 0) {
      Nb_TCA++;
    }
  }
  return Nb_TCA;
}

#endif // TCA_NRJ_LIB_H