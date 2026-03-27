# KXKM Batterie Parallelator 🎉🔋

![GitHub](https://img.shields.io/github/license/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub release (latest by date)](https://img.shields.io/github/v/release/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub Release Date](https://img.shields.io/github/release-date/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub last commit](https://img.shields.io/github/last-commit/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub issues](https://img.shields.io/github/issues/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub pull requests](https://img.shields.io/github/issues-pr/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub contributors](https://img.shields.io/github/contributors/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub forks](https://img.shields.io/github/forks/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub stars](https://img.shields.io/github/stars/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub watchers](https://img.shields.io/github/watchers/KomplexKapharnaum/KXKM_Batterie_Parallelator)
![GitHub followers](https://img.shields.io/github/followers/KomplexKapharnaum?label=Follow)

![GitHub Repo stars](https://img.shields.io/github/stars/KomplexKapharnaum/KXKM_Batterie_Parallelator?style=social)

![What is this](BMU.jpeg)
https://github.com/KomplexKapharnaum/KXKM_Batterie_Parallelator/blob/object-orriented/BMU.jpeg

## Description

Ce projet permet de gérer les batteries en parallèle, de les surveiller et d'enregistrer des logs sur une carte SD. Il utilise des INA pour mesurer la tension, le courant et la puissance des batteries, les GPIO I2C TCA pour contrôler les commutateurs de batterie et la bibliothèque SD pour enregistrer les données sur une carte SD. 🚀
C'est un projet open-source développé par [Komplex Kapharnaüm](COMING_SOON) pour les besoins de la [KXKM](COMING_SOON).

## Fonctionnalités

- **Serveur Web** 🌐 : Activer un serveur web pour contrôler les batteries à distance.
- **Seuils de tension et de courant** ⚡ : Définir des seuils de tension, de courant et de commutation.
- **Délai de reconnexion** ⏳ : Définir un délai de reconnexion et un nombre de commutations avant déconnexion.
- **Offset de tension** 🔋 : Définir un offset de différence de tension pour la déconnexion de la batterie.
- **Enregistrement des logs** 📝 : Définir un temps entre chaque enregistrement de log sur SD.
- **Courant de charge et de décharge** 🔌 : Définir un courant de décharge maximal et un courant de charge maximal.
- **Surveillance des batteries** 👀 : Connaître la batterie avec la tension maximale, la tension minimale et la tension moyenne.
- **État de la batterie** 📊 : Connaître l'état de la batterie, l'état de charge et de commuter la batterie.
- **Vérification de l'offset de tension** ✅ : Vérifier l'offset de différence de tension.

## Installation

1. Clonez ce dépôt GitHub :
    ```sh
    git clone https://github.com/votre-utilisateur/KXKM_Batterie_Parallelator.git
    ```

2. Ouvrez le projet avec PlatformIO.

3. Installez les dépendances nécessaires :
    ```sh
    pio lib install
    ```

## Configuration

Modifiez le fichier `platformio.ini` pour configurer votre environnement de développement. 🛠️

## Utilisation

1. Compilez et téléversez le code sur votre carte ESP32. 📲

2. Connectez-vous au réseau WiFi configuré dans le fichier `main.cpp`. 📶

3. Accédez à l'interface web via l'adresse IP de votre ESP32 pour contrôler les batteries et visualiser les logs. 🌍

### Guardrail mémoire firmware

```sh
scripts/check_memory_budget.sh --env kxkm-v3-16MB --ram-max 75 --flash-max 85
```

## Dépendances

- [Wire](https://github.com/arduino-libraries/Wire)
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO)
- [Timer](https://github.com/JChristensen/Timer)
- [Arduino EventEmitter](https://github.com/josephlarralde/ArduinoEventEmitter)
- [SD](https://github.com/arduino-libraries/SD)
- [EEPROM](https://github.com/arduino-libraries/EEPROM)
- [WiFi](https://github.com/arduino-libraries/WiFi)
- [SPI](https://github.com/arduino-libraries/SPI)
- [Ethernet](https://github.com/arduino-libraries/Ethernet)
- [ArtNet](https://github.com/hideakitai/ArtNet)
- [ESP32Ping](https://github.com/marian-craciunescu/ESP32Ping)
- [FS](https://github.com/arduino-libraries/FS)
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [TCA95x5_driver](https://github.com/karl-mohring/TCA95x5_driver)
- [TCA9555](https://github.com/RobTillaart/TCA9555)

## Structure du Projet

- `src/` : Contient les fichiers source du projet.
  - `BATTParallelator.cpp` : Implémentation de la classe BATTParallelator pour gérer les batteries en parallèle.
  - `BatteryManager.cpp` : Implémentation de la classe BatteryManager pour gérer les batteries.
  - `INA_NRJ_lib.cpp` : Implémentation de la classe INAHandler pour gérer les appareils INA.
  - `SD_Logger.cpp` : Implémentation de la classe SDLogger pour enregistrer les données sur la carte SD.
  - `WebServerHandler.cpp` : Implémentation de la classe WebServerHandler pour gérer le serveur web.
  - `main.cpp` : Point d'entrée principal du programme.
- `include/` : Contient les fichiers d'en-tête du projet.
  - `BATTParallelator_lib.h` : Déclaration des classes BATTParallelator et BatteryManager.
  - `INA_NRJ_lib.h` : Déclaration de la classe INAHandler.
  - `SD_Logger.h` : Déclaration de la classe SDLogger.
  - `WebServerHandler.h` : Déclaration de la classe WebServerHandler.
- `platformio.ini` : Fichier de configuration PlatformIO.

## Fonctionnement du Code

Le fichier `main.cpp` est le point d'entrée principal du programme. Voici un aperçu des principales déclarations et de leur fonctionnement :

- **Déclarations des constantes** :
  - `I2C_Speed` : Vitesse de l'I2C en KHz.
  - `set_min_voltage`, `set_max_voltage` : Seuils de tension minimale et maximale en mV.
  - `set_max_current`, `set_max_charge_current`, `set_max_discharge_current` : Seuils de courant maximal, de charge et de décharge en mA.
  - `reconnect_delay` : Délai de reconnexion de la batterie en ms.
  - `nb_switch_on` : Nombre de commutations avant déconnexion de la batterie.
  - `log_time` : Temps entre chaque enregistrement de log sur SD en secondes.

- **Déclarations des instances** :
  - `INAHandler inaHandler` : Instance de la classe INAHandler pour gérer les appareils INA.
  - `TCAHandler tcaHandler` : Instance de la classe TCAHandler pour gérer les appareils TCA.
  - `BATTParallelator BattParallelator` : Instance de la classe BATTParallelator pour gérer les batteries en parallèle.
  - `SDLogger sdLogger` : Instance de la classe SDLogger pour enregistrer les données sur la carte SD.
  - `BatteryManager batteryManager` : Instance de la classe BatteryManager pour gérer les batteries.
  - `WebServerHandler webServerHandler` : Instance de la classe WebServerHandler pour gérer le serveur web (si activé).

- **Fonctions principales** :
  - `setup()` : Fonction d'initialisation du programme. Configure les appareils I2C, les INA, les TCA, le logger SD et les paramètres de gestion des batteries. Démarre également les tâches FreeRTOS pour lire les données des INA, enregistrer les données sur la carte SD et vérifier la tension des batteries.
  - `loop()` : Fonction principale de la boucle. Gère les requêtes du serveur web (si activé).

- **Tâches FreeRTOS** :
  - `readINADataTask(void *pvParameters)` : Tâche pour lire les données des INA.
  - `logDataTask(void *pvParameters)` : Tâche pour enregistrer les données sur la carte SD.
  - `checkBatteryVoltagesTask(void *pvParameters)` : Tâche pour vérifier la tension des batteries.

## Fichiers KiCad

Le dépôt contient également des fichiers KiCad pour les cartes matérielles suivantes :

- **Carte Mère** : Chaine jusqu'à un total de 4 cartes pour un total de 16 batteries. Comprend des shunts et des puces INA.
- **Carte Isolateur/Amplificateur I2C** : Pour isoler et amplifier les signaux I2C.
- **Cartes d'Extension** : 4 cartes d'extension par carte mère basées sur des MOSFET prévus pour 40A.
- **Carte Relais** : En cours de développement.

## Licence

Ce projet est sous licence GNU General Public License v3.0. Voir le fichier [LICENSE](LICENSE) pour plus de détails. 📜

## Compagnie

Ce projet a été développé par la compagnie [Komplex Kapharnaum](https://www.komplex-kapharnaum.net/), une compagnie de création artistique basée à Lyon, France. Komplex Kapharnaum crée des spectacles et des installations artistiques en utilisant des technologies innovantes pour transformer l'espace public et offrir des expériences immersives au public.
