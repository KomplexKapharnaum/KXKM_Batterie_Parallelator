/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file TCA_NRJ_lib.h
 * @brief Déclaration de la classe TCAHandler pour gérer les appareils TCA.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef TCA_NRJ_LIB_H
#define TCA_NRJ_LIB_H

#include "TCA9555.h" // Bibliothèque TCA9555

/**
 * @class TCAHandler
 * @brief Classe pour gérer les appareils TCA.
 */
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

#endif // TCA_NRJ_LIB_H