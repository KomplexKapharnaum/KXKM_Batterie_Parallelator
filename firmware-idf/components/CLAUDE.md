# ESP-IDF Components

25 composants modulaires, namespace `bmu_*`. Chaque composant : `CMakeLists.txt`, `Kconfig`, `include/bmu_X.h`, `bmu_X.cpp`.

## Adding a New Component

1. Créer `bmu_xyz/CMakeLists.txt` :
   ```cmake
   idf_component_register(
       SRCS "bmu_xyz.cpp"
       INCLUDE_DIRS "include"
       REQUIRES bmu_protection bmu_config  # dépendances
       PRIV_REQUIRES mbedtls                # privées
   )
   ```
2. Header dans `include/bmu_xyz.h` avec garde `extern "C"`
3. Ajouter au `REQUIRES` de `firmware-idf/main/CMakeLists.txt` si appelé depuis main
4. Kconfig si paramètres configurables

## Conventions

- Une fonction `bmu_xyz_init()` qui prend les contextes nécessaires
- Tâches FreeRTOS : nommer `xyz_task`, stack via Kconfig (`CONFIG_BMU_XYZ_TASK_STACK`)
- État partagé : mutex via `xSemaphoreCreateMutex()`, timeout 20-100ms
- Logs : `static const char *TAG = "XYZ";` puis `ESP_LOGI(TAG, ...)`

## GATT BLE

Si exposition BLE nécessaire :
- Battery service (0x0001) : chars 0x0010-0x002F (batteries) + 0x0038-0x003B (R_int/SOH/balancer)
- System service (0x0002) : 0x0020-0x0026 (FW/heap/uptime/IP/topo/solar/Victron scan)
- Control service (0x0003) : 0x0030-0x0038 (write commands, WRITE_ENC pour mutations)

Pattern : déclarer struct packed → UUID → callback read/write → ajouter au `s_X_chr_defs[]` → notify dans timer.

## Anti-Patterns

- Ne pas mettre de blocking calls (sleep, mutex >100ms) dans une tâche périodique
- Ne pas oublier le NULL terminator dans les arrays `ble_gatt_chr_def[]`
- Ne pas hardcoder les stack sizes — utiliser Kconfig
- Ne pas log en LOGD dans une fonction critique (filtré par défaut, debug invisible)
