#ifndef PIR_H
#define PIR_H

#include <stdint.h>
#include <stdbool.h>

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
 * @return true wenn Bewegung erkannt, false sonst
 */
bool pir_motion_detected(void);

/**
 * @brief Letzte Bewegungszeit abrufen
 * @return Zeitstempel der letzten Bewegung in µs
 */
uint64_t pir_get_last_motion_time(void);

/**
 * @brief PIR Cooldown-Status abrufen
 * @return true wenn Cooldown aktiv, false sonst
 */
bool pir_is_cooldown(void);

#endif // PIR_H
