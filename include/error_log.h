#ifndef ERROR_LOG_H
#define ERROR_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_err.h"
#include "config.h"

// Fehlercode-Definitionen
#define ERR_STACK_OVERFLOW      0x01
#define ERR_WATCHDOG_TRIGGER    0x02
#define ERR_SERVO_FAILURE       0x03
#define ERR_BATTERY_CRITICAL    0x04
#define ERR_PIR_FAILURE         0x05
#define ERR_ADC_FAILURE         0x06
#define ERR_NVS_FAILURE         0x07
#define ERR_WIFI_FAILURE        0x08
#define ERR_OTA_FAILURE         0x09
#define ERR_MEMORY_ALLOC        0x0A

typedef struct {
    char code[ERROR_CODE_LENGTH + 1];  // Fehlercode (z.B. "E0012345")
    uint8_t error_id;                   // Fehler-ID
    uint64_t timestamp_ms;              // Zeitstempel in ms
    char task_name[16];                 // Task-Name
    uint8_t severity;                   // Schweregrad (0=Info, 1=Warning, 2=Error, 3=Critical)
} error_log_entry_t;

/**
 * @brief Error-Log System initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t error_log_init(void);

/**
 * @brief Fehler loggen
 * @param error_id Fehler-ID
 * @param task_name Task-Name (oder NULL für global)
 * @param severity Schweregrad (0=Info, 1=Warning, 2=Error, 3=Critical)
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t error_log_add(uint8_t error_id, const char *task_name, uint8_t severity);

/**
 * @brief Fehlercode-Text abrufen
 * @param error_id Fehler-ID
 * @return Beschreibungstext
 */
const char* error_log_get_text(uint8_t error_id);

/**
 * @brief Alle Fehler-Log-Einträge abrufen
 * @param entries Ausgabe-Array
 * @param max_entries Maximale Anzahl
 * @return Anzahl der Einträge
 */
uint16_t error_log_get_all(error_log_entry_t *entries, uint16_t max_entries);

/**
 * @brief Fehler-Log leeren
 */
void error_log_clear(void);

/**
 * @brief Fehlercode generieren
 * @param error_id Fehler-ID
 * @param timestamp_ms Zeitstempel
 * @return Generierter Fehlercode-String
 */
void error_log_generate_code(char *code, uint8_t error_id, uint64_t timestamp_ms);

#endif // ERROR_LOG_H
