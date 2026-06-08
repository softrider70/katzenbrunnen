#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Telegram-Modul Initialisierung
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_init(void);

/**
 * @brief Telegram-Nachricht senden
 * @param message Nachrichtentext (UTF-8)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_send_message(const char *message);

/**
 * @brief Telegram-Nachricht mit Markdown-Formatierung senden
 * @param message Nachrichtentext mit Markdown (UTF-8)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_send_message_markdown(const char *message);

/**
 * @brief Bot Token in NVS speichern
 * @param token Bot Token (max 256 Zeichen)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_save_token(const char *token);

/**
 * @brief Chat ID in NVS speichern
 * @param chat_id Chat ID (max 64 Zeichen)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_save_chat_id(const char *chat_id);

/**
 * @brief Prüfen ob Telegram konfiguriert ist
 * @return true wenn Token und Chat ID gesetzt, sonst false
 */
bool telegram_is_configured(void);

/**
 * @brief Telegram-Nachrichten aktivieren/deaktivieren
 * @param enabled true für aktiviert, false für deaktiviert
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_set_enabled(bool enabled);

/**
 * @brief Prüfen ob Telegram-Nachrichten aktiviert sind
 * @return true wenn aktiviert, sonst false
 */
bool telegram_is_enabled(void);

/**
 * @brief Telegram-Nacht-Startzeit setzen (0-23 Uhr)
 * @param hour Stunde (0-23)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_set_night_start_hour(int hour);

/**
 * @brief Telegram-Nacht-Stoppzeit setzen (0-23 Uhr)
 * @param hour Stunde (0-23)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t telegram_set_night_end_hour(int hour);

/**
 * @brief Telegram-Nacht-Startzeit abrufen
 * @return Stunde (0-23)
 */
int telegram_get_night_start_hour(void);

/**
 * @brief Telegram-Nacht-Stoppzeit abrufen
 * @return Stunde (0-23)
 */
int telegram_get_night_end_hour(void);

/**
 * @brief Telegram-Modul deinitialisieren
 */
void telegram_deinit(void);

/**
 * @brief Prüfen ob Nacht-Modus aktiv ist
 * @return true wenn Nacht-Modus, sonst false
 */
bool telegram_is_night_mode(void);

/**
 * @brief Gepufferte Nacht-Nachrichten senden (um Stoppzeit aufrufen)
 */
void telegram_send_night_buffer(void);

/**
 * @brief Nacht-Event puffern (Startzeit oder Dauer)
 * @param event_text Event-Text im Format "HH:MM" für Start oder "HH:MM+Xs" für Dauer
 */
void telegram_buffer_night_event(const char *event_text);

#endif // TELEGRAM_H
