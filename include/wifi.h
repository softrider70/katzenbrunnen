#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief WiFi-Modul Initialisierung
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t wifi_init(void);

/**
 * @brief WiFi-Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t wifi_start_task(void);

/**
 * @brief WiFi-Verbindungsstatus abrufen
 * @return true wenn verbunden, false sonst
 */
bool wifi_is_connected(void);

/**
 * @brief WiFi-SSID abrufen
 * @param ssid Ausgabe-Buffer (mindestens 32 Bytes)
 */
void wifi_get_ssid(char *ssid);

/**
 * @brief WiFi-RSSI abrufen
 * @return RSSI-Wert in dBm (0 wenn nicht verbunden)
 */
int8_t wifi_get_rssi(void);

/**
 * @brief WiFi-IP abrufen
 * @param ip_str Ausgabe-Buffer (mindesten 16 Bytes)
 */
void wifi_get_ip(char *ip_str);

/**
 * @brief WiFi-Credentials konfigurieren
 * @param ssid SSID
 * @param password Passwort
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t wifi_set_credentials(const char *ssid, const char *password);

/**
 * @brief WiFi-Verbindung neu starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t wifi_reconnect(void);

/**
 * @brief AP-Modus-Status abrufen
 * @return true wenn AP-Modus erzwungen ist, false sonst
 */
bool wifi_is_ap_mode_forced(void);

/**
 * @brief WiFi-Credentials löschen (WiFi-Reset)
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t wifi_reset_credentials(void);

/**
 * @brief WiFi-Modul deinitialisieren (Mutex cleanup)
 */
void wifi_deinit(void);

#endif // WIFI_H
