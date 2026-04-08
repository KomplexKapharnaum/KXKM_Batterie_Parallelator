// components/bmu_types/include/bmu_types.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Forward declarations (ESP-IDF) ──
// Avoid including full ESP-IDF headers in host tests
#ifdef NATIVE_TEST
    typedef void* i2c_master_bus_handle_t;
    typedef void* i2c_master_dev_handle_t;
    typedef void* SemaphoreHandle_t;
    typedef void* QueueHandle_t;
    typedef int esp_err_t;
    #define ESP_OK 0
    #define ESP_FAIL -1
    #define ESP_ERR_TIMEOUT 0x107
#else
    #include "driver/i2c_master.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
    #include "freertos/queue.h"
#endif

// ── Constants ──
// BMU_MAX_BATTERIES may already be defined by bmu_config.h
#ifndef BMU_MAX_BATTERIES
#define BMU_MAX_BATTERIES       32
#endif
#define BMU_MAX_BUSES           2
#define BMU_HEALTH_SCORE_MAX    100
#define BMU_HEALTH_SCORE_INIT   100
#define BMU_HEALTH_OK_INCR      5
#define BMU_HEALTH_FAIL_DECR    20
#define BMU_HEALTH_THRESH_WARN  60
#define BMU_HEALTH_THRESH_CRIT  30
#define BMU_HEALTH_THRESH_RECONNECT 60

// ── Battery state (unchanged enum values) ──
typedef enum {
    BMU_STATE_CONNECTED = 0,
    BMU_STATE_DISCONNECTED,
    BMU_STATE_RECONNECTING,
    BMU_STATE_ERROR,
    BMU_STATE_LOCKED
} bmu_battery_state_t;

// ── Device health ──
typedef struct {
    uint8_t score;          // 0-100
    uint8_t consec_fails;   // for bus recovery quorum
} bmu_device_health_t;

// ── I2C bus abstraction ──
typedef struct {
    i2c_master_bus_handle_t handle;
    SemaphoreHandle_t       mutex;
    uint8_t                 bus_id;     // 0=DOCK hw, 1=bitbang
    bmu_device_health_t     bus_health;
} bmu_i2c_bus_t;

// ── Device abstraction (one per INA237 or TCA9535) ──
typedef struct {
    uint8_t                 bus_id;
    uint8_t                 local_idx;  // 0-15 on this bus
    uint8_t                 global_idx; // 0-31
    uint8_t                 addr;       // I2C address
    i2c_master_dev_handle_t handle;
    bmu_device_health_t     health;
} bmu_device_t;

// ── Immutable snapshot (produced by protection each cycle) ──
typedef struct {
    uint32_t timestamp_ms;
    uint16_t cycle_count;
    uint8_t  nb_batteries;
    bool     topology_ok;

    struct {
        float               voltage_mv;
        float               current_a;
        bmu_battery_state_t state;
        uint8_t             health_score;
        uint8_t             nb_switches;
        bool                balancer_active;
    } battery[BMU_MAX_BATTERIES];

    float fleet_max_mv;
    float fleet_mean_mv;
} bmu_snapshot_t;

// ── Command types (cmd queue → protection) ──
typedef enum {
    CMD_TOPOLOGY_CHANGED,
    CMD_BALANCE_REQUEST,
    CMD_WEB_SWITCH,
    CMD_CONFIG_UPDATE,
    CMD_BUS_RECOVERY,
} bmu_cmd_type_t;

typedef struct {
    bmu_cmd_type_t type;
    union {
        struct {
            uint8_t nb_ina;
            uint8_t nb_tca;
        } topology;
        struct {
            uint8_t battery_idx;
            bool    on;
        } balance_req;
        struct {
            uint8_t battery_idx;
            bool    on;
        } web_switch;
        struct {
            uint8_t bus_id;
        } bus_recovery;
    } payload;
} bmu_cmd_t;

#ifdef __cplusplus
}
#endif
