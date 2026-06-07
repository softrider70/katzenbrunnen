#ifndef PIR_H
#define PIR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief PIR-Modul Initialisierung
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t pir_init(void);

/**
 * @brief PIR-Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t pir_start_task(void);

/**
 * @brief Bewegungserkennungs-Status abrufen
 * @return true wenn innerhalb des Erkennungsfensters Bewegung war
 */
bool pir_motion_detected(void);

/**
 * @brief Aktuelles GPIO-Level des PIR-Sensors abrufen
 * @return true wenn GPIO HIGH ist, false wenn LOW
 */
bool pir_get_gpio_level(void);

/**
 * @brief Letzte Bewegungszeit abrufen
 * @return Zeitstempel der letzten Bewegung in µs (0 = noch keine)
 */
uint64_t pir_get_last_motion_time(void);

#endif // PIR_H
