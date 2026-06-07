#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Servo-Modul Initialisierung
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t servo_init(void);

/**
 * @brief Servo-Kalibrierung (Bewegungs-Test nach Systemstart)
 */
void servo_calibrate(void);

/**
 * @brief Servo auf Position bewegen
 * @param pulse_us Pulsweite in Mikrosekunden
 */
void servo_set_position(uint32_t pulse_us);

/**
 * @brief Servo auf Position bewegen mit FET-Aktivierung
 * @param pulse_us Pulsweite in Mikrosekunden
 * @param fet_duration_ms FET-Aktivierungsdauer in Millisekunden
 */
void servo_set_position_with_fet(uint32_t pulse_us, uint32_t fet_duration_ms);

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

/**
 * @brief Servo-Modul deinitialisieren (Mutex cleanup)
 */
void servo_deinit(void);

#endif // SERVO_H
