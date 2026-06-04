#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Watchdog (TWDT) initialisieren/rekonfigurieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t watchdog_init(void);

/**
 * @brief Watchdog aktiv markieren (Kompatibilität, kein eigener Task mehr)
 * @return ESP_OK
 */
esp_err_t watchdog_start_task(void);

/**
 * @brief Aktuellen Task beim Watchdog anmelden
 * Muss aus dem zu überwachenden Task aufgerufen werden.
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t watchdog_subscribe(void);

/**
 * @brief Watchdog für aktuellen (angemeldeten) Task zurücksetzen/füttern
 */
void watchdog_feed(void);

/**
 * @brief Watchdog-Reset für aktuellen Task (Alias zu watchdog_feed)
 */
void watchdog_reset(void);

/**
 * @brief Watchdog-Status abrufen
 * @return true wenn Watchdog aktiv, false sonst
 */
bool watchdog_is_active(void);

#endif // WATCHDOG_H
