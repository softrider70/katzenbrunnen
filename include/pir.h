#ifndef PIR_H
#define PIR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

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
 * @return true wenn innerhalb des Erkennungsfensters Bewegung war
 */
bool pir_motion_detected(void);

/**
 * @brief Letzte Bewegungszeit abrufen
 * @return Zeitstempel der letzten Bewegung in µs (0 = noch keine)
 */
uint64_t pir_get_last_motion_time(void);

// Maximale Anzahl gespeicherter Bewegungsereignisse (Ringbuffer)
#define PIR_EVENT_MAX_COUNT 50

/**
 * @brief Bewegungsereignis-Struktur
 */
typedef struct {
    uint64_t timestamp_ms;    // Zeitstempel in ms
    uint32_t duration_ms;     // Dauer der Bewegung in ms
} pir_event_t;

/**
 * @brief Bewegungsereignis hinzufügen (wird von control_task aufgerufen)
 * @param duration_ms Dauer der Bewegung in ms
 */
void pir_add_event(uint64_t duration_ms);

/**
 * @brief Bewegungsereignisse abrufen
 * @param events Buffer für Ereignisse
 * @param max_count Maximale Anzahl der abzurufenden Ereignisse
 * @return Anzahl der abgerufenen Ereignisse
 */
uint16_t pir_get_events(pir_event_t *events, uint16_t max_count);

/**
 * @brief Bewegungsereignisse löschen
 */
void pir_clear_events(void);

#endif // PIR_H
