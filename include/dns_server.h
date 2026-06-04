#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

/**
 * @brief DNS-Server für Captive Portal starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t dns_server_start(void);

/**
 * @brief DNS-Server stoppen
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t dns_server_stop(void);

#endif // DNS_SERVER_H
