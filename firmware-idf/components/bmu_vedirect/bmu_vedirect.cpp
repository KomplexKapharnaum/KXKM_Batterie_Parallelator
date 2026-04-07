/**
 * @file bmu_vedirect.cpp
 * @brief Victron VE.Direct TEXT protocol parser (UART)
 *
 * Implémente le parsing du protocole TEXT VE.Direct utilisé par les
 * chargeurs solaires Victron (MPPT). Les trames arrivent toutes les
 * secondes sur UART 19200 8N1.
 *
 * Format : LABEL\tVALUE\r\n  (terminé par Checksum\t<byte>)
 */

#include "bmu_vedirect.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <cstdlib>

static const char *TAG = "VEDR";

// ---------------------------------------------------------------------------
// Compile-time gate : si CONFIG_BMU_VEDIRECT_ENABLED n'est pas défini,
// toutes les fonctions sont des no-ops.
// ---------------------------------------------------------------------------
#if !defined(CONFIG_BMU_VEDIRECT_ENABLED) || !CONFIG_BMU_VEDIRECT_ENABLED

esp_err_t bmu_vedirect_init(void) { return ESP_ERR_NOT_SUPPORTED; }
const bmu_vedirect_data_t *bmu_vedirect_get_data(void) { return nullptr; }
bool bmu_vedirect_is_connected(void) { return false; }
const char *bmu_vedirect_cs_name(uint8_t) { return "Disabled"; }

#else // CONFIG_BMU_VEDIRECT_ENABLED == 1

// ---------------------------------------------------------------------------
// Constantes
// ---------------------------------------------------------------------------
static constexpr size_t   RX_BUF_SIZE        = 1024;
static constexpr size_t   MAX_LABEL_LEN      = 16;
static constexpr size_t   MAX_VALUE_LEN      = 32;
static constexpr uint32_t TASK_STACK_SIZE     = CONFIG_BMU_VEDIRECT_TASK_STACK;
static constexpr int      TASK_PRIORITY       = CONFIG_BMU_VEDIRECT_TASK_PRIORITY;
static constexpr int64_t  CONNECTION_TIMEOUT_MS = 5000;

// ---------------------------------------------------------------------------
// État interne
// ---------------------------------------------------------------------------
enum class ParseState : uint8_t {
    IDLE,
    LABEL,
    VALUE,
    CHECKSUM,
};

// Double-buffer : le parser écrit dans staging_, puis copie vers public_
static bmu_vedirect_data_t s_public;
static bmu_vedirect_data_t s_staging;
static portMUX_TYPE        s_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool                s_initialized = false;

// ---------------------------------------------------------------------------
// parse_field — mappe un label VE.Direct vers le champ correspondant
// ---------------------------------------------------------------------------
static void parse_field(const char *label, const char *value,
                        bmu_vedirect_data_t *frame)
{
    if (strcmp(label, "V") == 0) {
        frame->battery_voltage_v = atoi(value) / 1000.0f;
    } else if (strcmp(label, "I") == 0) {
        frame->battery_current_a = atoi(value) / 1000.0f;
    } else if (strcmp(label, "VPV") == 0) {
        frame->panel_voltage_v = atoi(value) / 1000.0f;
    } else if (strcmp(label, "PPV") == 0) {
        frame->panel_power_w = (uint16_t)atoi(value);
    } else if (strcmp(label, "CS") == 0) {
        frame->charge_state = (uint8_t)atoi(value);
    } else if (strcmp(label, "MPPT") == 0) {
        frame->mppt_state = (uint8_t)atoi(value);
    } else if (strcmp(label, "ERR") == 0) {
        frame->error_code = (uint8_t)atoi(value);
    } else if (strcmp(label, "H19") == 0) {
        frame->yield_total_wh = (uint32_t)(atoi(value) * 10);  // 0.01kWh → Wh
    } else if (strcmp(label, "H20") == 0) {
        frame->yield_today_wh = (uint32_t)(atoi(value) * 10);
    } else if (strcmp(label, "H21") == 0) {
        frame->max_power_today_w = (uint16_t)atoi(value);
    } else if (strcmp(label, "PID") == 0) {
        strncpy(frame->product_id, value, sizeof(frame->product_id) - 1);
        frame->product_id[sizeof(frame->product_id) - 1] = '\0';
    } else if (strcmp(label, "SER#") == 0) {
        strncpy(frame->serial, value, sizeof(frame->serial) - 1);
        frame->serial[sizeof(frame->serial) - 1] = '\0';
    } else if (strcmp(label, "FW") == 0) {
        strncpy(frame->firmware, value, sizeof(frame->firmware) - 1);
        frame->firmware[sizeof(frame->firmware) - 1] = '\0';
    } else if (strcmp(label, "LOAD") == 0) {
        frame->load_on = (strcmp(value, "ON") == 0);
    }
}

// ---------------------------------------------------------------------------
// charge_state_name
// ---------------------------------------------------------------------------
const char *bmu_vedirect_cs_name(uint8_t cs)
{
    switch (cs) {
        case 0:   return "Off";
        case 2:   return "Fault";
        case 3:   return "Bulk";
        case 4:   return "Absorption";
        case 5:   return "Float";
        case 7:   return "Equalize";
        case 245: return "Starting";
        case 252: return "External";
        default:  return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Tâche FreeRTOS — lecture UART + machine d'état
// ---------------------------------------------------------------------------
static void vedirect_task(void * /*arg*/)
{
    uint8_t  rx_byte;
    char     label[MAX_LABEL_LEN];
    char     value[MAX_VALUE_LEN];
    uint8_t  label_idx = 0;
    uint8_t  value_idx = 0;
    uint8_t  checksum  = 0;         // somme glissante de tous les octets
    auto     state     = ParseState::IDLE;

    const uart_port_t port = (uart_port_t)CONFIG_BMU_VEDIRECT_UART_NUM;

    // Staging buffer nettoyé au démarrage
    memset(&s_staging, 0, sizeof(s_staging));

    ESP_LOGI(TAG, "Tâche VE.Direct démarrée (UART%d RX=%d TX=%d @ %d baud)",
             CONFIG_BMU_VEDIRECT_UART_NUM,
             CONFIG_BMU_VEDIRECT_RX_GPIO,
             CONFIG_BMU_VEDIRECT_TX_GPIO,
             CONFIG_BMU_VEDIRECT_BAUD);

    for (;;) {
        int len = uart_read_bytes(port, &rx_byte, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;  // timeout — pas de données
        }

        // Tous les octets participent au checksum (y compris \r, \n, \t)
        checksum += rx_byte;

        switch (state) {
        // ----- IDLE : on attend le début d'un label ('\r' ou '\n' ignorés)
        case ParseState::IDLE:
            if (rx_byte == '\r' || rx_byte == '\n') {
                // rester en IDLE
            } else {
                // Premier caractère d'un label
                label_idx = 0;
                label[label_idx++] = (char)rx_byte;
                state = ParseState::LABEL;
            }
            break;

        // ----- LABEL : accumulation jusqu'au '\t'
        case ParseState::LABEL:
            if (rx_byte == '\t') {
                label[label_idx] = '\0';
                value_idx = 0;

                // Le label "Checksum" est spécial : le prochain octet
                // est la valeur de checksum brute (pas un texte).
                if (strcmp(label, "Checksum") == 0) {
                    state = ParseState::CHECKSUM;
                } else {
                    state = ParseState::VALUE;
                }
            } else {
                if (label_idx < MAX_LABEL_LEN - 1) {
                    label[label_idx++] = (char)rx_byte;
                }
            }
            break;

        // ----- VALUE : accumulation jusqu'au '\r' ou '\n'
        case ParseState::VALUE:
            if (rx_byte == '\r' || rx_byte == '\n') {
                value[value_idx] = '\0';
                parse_field(label, value, &s_staging);
                state = ParseState::IDLE;
            } else {
                if (value_idx < MAX_VALUE_LEN - 1) {
                    value[value_idx++] = (char)rx_byte;
                }
            }
            break;

        // ----- CHECKSUM : l'octet après "Checksum\t" complète la somme
        case ParseState::CHECKSUM:
            // L'octet de checksum a déjà été ajouté à `checksum` plus haut.
            // Si le protocole est correct, checksum == 0 (mod 256).
            if ((checksum & 0xFF) == 0) {
                // Trame valide — publication atomique
                s_staging.valid = true;
                s_staging.last_update_ms =
                    esp_timer_get_time() / 1000;  // µs → ms

                portENTER_CRITICAL(&s_spinlock);
                memcpy(&s_public, &s_staging, sizeof(s_public));
                portEXIT_CRITICAL(&s_spinlock);

                ESP_LOGD(TAG, "Trame OK  V=%.2fV  I=%.2fA  CS=%s",
                         s_staging.battery_voltage_v,
                         s_staging.battery_current_a,
                         bmu_vedirect_cs_name(s_staging.charge_state));
            } else {
                ESP_LOGW(TAG, "Checksum invalide (0x%02X), trame ignorée",
                         checksum & 0xFF);
            }

            // Réinitialisation pour la prochaine trame
            memset(&s_staging, 0, sizeof(s_staging));
            checksum = 0;
            state = ParseState::IDLE;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------
esp_err_t bmu_vedirect_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const uart_port_t port = (uart_port_t)CONFIG_BMU_VEDIRECT_UART_NUM;

    const uart_config_t uart_cfg = {
        .baud_rate  = CONFIG_BMU_VEDIRECT_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(port, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config échoué: %s", esp_err_to_name(err));
        return err;
    }

    int tx_pin = CONFIG_BMU_VEDIRECT_TX_GPIO;
    if (tx_pin < 0) {
        tx_pin = UART_PIN_NO_CHANGE;
    }

    err = uart_set_pin(port,
                       tx_pin,
                       CONFIG_BMU_VEDIRECT_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin échoué: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(port, RX_BUF_SIZE, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install échoué: %s", esp_err_to_name(err));
        return err;
    }

    // Données publiques initialisées à zéro
    memset(&s_public, 0, sizeof(s_public));

    BaseType_t ret = xTaskCreate(
        vedirect_task,
        "vedirect",
        TASK_STACK_SIZE,
        nullptr,
        TASK_PRIORITY,
        nullptr);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Création tâche VE.Direct échouée");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "VE.Direct initialisé sur UART%d", CONFIG_BMU_VEDIRECT_UART_NUM);
    return ESP_OK;
}

const bmu_vedirect_data_t *bmu_vedirect_get_data(void)
{
    return &s_public;
}

bool bmu_vedirect_is_connected(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t last;

    portENTER_CRITICAL(&s_spinlock);
    last = s_public.last_update_ms;
    portEXIT_CRITICAL(&s_spinlock);

    return (last > 0) && ((now_ms - last) < CONNECTION_TIMEOUT_MS);
}

#endif // CONFIG_BMU_VEDIRECT_ENABLED
