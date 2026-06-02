#ifndef STACK_MONITOR_H
#define STACK_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    char task_name[16];
    uint32_t stack_size;
    uint32_t stack_free;
    uint8_t stack_percent;  // 0-100
    bool warning;           // >60%
    bool critical;          // >80%
} stack_info_t;

/**
 * @brief Stack-Monitor initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t stack_monitor_init(void);

/**
 * @brief Stack-Monitor Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t stack_monitor_start_task(void);

/**
 * @brief Stack-Informationen aller Tasks abrufen
 * @param info Ausgabe-Array
 * @param max_tasks Maximale Anzahl
 * @return Anzahl der Tasks
 */
uint8_t stack_monitor_get_all(stack_info_t *info, uint8_t max_tasks);

/**
 * @brief Prüfen ob Task Stack-Warning hat
 * @param task_name Task-Name
 * @return true wenn >60%, false sonst
 */
bool stack_monitor_has_warning(const char *task_name);

#endif // STACK_MONITOR_H
