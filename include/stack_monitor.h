#ifndef STACK_MONITOR_H
#define STACK_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    char task_name[16];
    uint32_t stack_size;    // Gesamtgröße (0 = nicht ermittelbar)
    uint32_t stack_free;    // Freier Reststack in Bytes (High-Water-Mark)
    uint8_t stack_percent;  // 0 (ohne Gesamtgröße nicht bestimmbar)
    bool warning;           // freier Stack < STACK_FREE_WARNING_BYTES
    bool critical;          // freier Stack < STACK_FREE_CRITICAL_BYTES
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

/**
 * @brief Stack-Monitor deinitialisieren (Mutex cleanup)
 */
void stack_monitor_deinit(void);

#endif // STACK_MONITOR_H
