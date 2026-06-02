#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Servo-Modul Initialisierung
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t servo_init(void);

/**
 * @brief Servo auf Position bewegen
 * @param pulse_us Pulsweite in Mikrosekunden
 */
void servo_set_position(uint32_t pulse_us);

/**
 * @brief Wasserhahn öffnen
 */
void servo_open_valve(void);

/**
 * @brief Wasserhahn schließen
 */
void servo_close_valve(void);

/**
 * @brief Wasserhahn-Status abrufen
 * @return true wenn geöffnet, false wenn geschlossen
 */
bool servo_is_valve_open(void);

/**
 * @brief Emergency-Close (für Watchdog/Stack-Overflow)
 * Wird direkt ohne Mutex aufgerufen, da bei kritischen Fehlern
 */
void servo_emergency_close(void);

#endif // SERVO_H
