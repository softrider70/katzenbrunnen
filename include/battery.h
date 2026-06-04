#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Battery-Modul Initialisierung
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t battery_init(void);

/**
 * @brief Aktuelle Batteriespannung abrufen
 * @return Spannung in Volt
 */
float battery_get_voltage(void);

/**
 * @brief Aktuellen Batterie-Prozentwert abrufen
 * @return Prozent (0-100)
 */
uint8_t battery_get_percent(void);

/**
 * @brief Batterie-Status prüfen
 * @return true wenn kritisch, false sonst
 */
bool battery_is_critical(void);

/**
 * @brief Battery-Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t battery_start_task(void);

/**
 * @brief Battery-Modul deinitialisieren (Mutex cleanup)
 */
void battery_deinit(void);

#endif // BATTERY_H
