#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Watchdog initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t watchdog_init(void);

/**
 * @brief Watchdog Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t watchdog_start_task(void);

/**
 * @brief Watchdog-Reset für aktuellen Task
 */
void watchdog_reset(void);

/**
 * @brief Watchdog-Status abrufen
 * @return true wenn Watchdog aktiv, false sonst
 */
bool watchdog_is_active(void);

#endif // WATCHDOG_H
