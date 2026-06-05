#ifndef LED_H
#define LED_H

#include "esp_err.h"

// System-Status, der über die WS2812B RGB-LED (GPIO48) angezeigt wird
typedef enum {
    LED_STATE_OFF = 0,    // aus
    LED_STATE_IDLE,       // grün gedimmt - System bereit, Ventil zu
    LED_STATE_MOTION,     // gelb - Bewegung erkannt (noch nicht geöffnet)
    LED_STATE_OPEN,       // blau - Wasserhahn offen
    LED_STATE_CRITICAL,   // rot - kritische Batteriespannung
} led_state_t;

/**
 * @brief WS2812B RGB-LED initialisieren (RMT auf GPIO_LED_DATA)
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t led_init(void);

/**
 * @brief Status-Farbe der RGB-LED setzen
 * @param state Anzuzeigender System-Status
 */
void led_set_state(led_state_t state);

#endif // LED_H
