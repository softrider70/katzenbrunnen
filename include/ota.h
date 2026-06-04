#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief OTA-Modul initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t ota_init(void);

/**
 * @brief OTA-Update starten
 * @param url URL zur Firmware-Datei
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t ota_start_update(const char *url);

/**
 * @brief OTA-Status abrufen
 * @param in_progress Ausgabe: OTA läuft
 * @param last_result_ok Ausgabe: Letztes Ergebnis OK
 * @param phase Ausgabe-Buffer für Phase (mindestens 32 Bytes)
 * @param message Ausgabe-Buffer für Nachricht (mindesten 64 Bytes)
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t ota_get_status(bool *in_progress, bool *last_result_ok, char *phase, char *message);

/**
 * @brief OTA-Rollback ausführen
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t ota_rollback(void);

/**
 * @brief OTA-Modul deinitialisieren (Mutex cleanup)
 */
void ota_deinit(void);

#endif // OTA_H
