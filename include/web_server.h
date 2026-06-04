#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Web-Server initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t web_server_init(void);

/**
 * @brief Web-Server starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t web_server_start(void);

/**
 * @brief Web-Server stoppen
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t web_server_stop(void);

#endif // WEB_SERVER_H
