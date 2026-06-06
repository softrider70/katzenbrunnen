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

// Maximale Anzahl gespeicherter Öffnungsereignisse (Ringbuffer)
#define SERVO_EVENT_MAX_COUNT 50

/**
 * @brief Öffnungsereignis-Struktur
 */
typedef struct {
    uint64_t timestamp_ms;    // Zeitstempel in ms
    uint32_t duration_ms;     // Dauer der Öffnung in ms
} servo_event_t;

/**
 * @brief Öffnungsereignisse abrufen
 * @param events Buffer für Ereignisse
 * @param max_count Maximale Anzahl der abzurufenden Ereignisse
 * @return Anzahl der abgerufenen Ereignisse
 */
uint16_t servo_get_events(servo_event_t *events, uint16_t max_count);

/**
 * @brief Öffnungsereignisse löschen
 */
void servo_clear_events(void);

/**
 * @brief Servo-Modul deinitialisieren (Mutex cleanup)
 */
void servo_deinit(void);

#endif // SERVO_H
