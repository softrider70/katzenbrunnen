#ifndef HEAP_MONITOR_H
#define HEAP_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Heap-Status-Struktur
 */
typedef struct {
    uint32_t total_heap;       // Gesamter Heap in Bytes
    uint32_t free_heap;        // Freier Heap in Bytes
    uint32_t min_free_heap;    // Minimal freier Heap seit Start in Bytes
    uint32_t largest_free_block; // Größter zusammenhängender freier Block in Bytes
    uint8_t free_percent;      // Freier Heap in Prozent
    bool warning;              // Warnung wenn freier Heap unter Schwelle
    bool critical;             // Kritisch wenn freier Heap unter Schwelle
} heap_info_t;

/**
 * @brief Heap-Monitor initialisieren
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t heap_monitor_init(void);

/**
 * @brief Heap-Monitor-Task starten
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t heap_monitor_start_task(void);

/**
 * @brief Aktuelle Heap-Informationen abrufen
 * @param info Ausgabe-Buffer für Heap-Informationen
 * @return ESP_OK bei Erfolg, Fehlercode sonst
 */
esp_err_t heap_monitor_get_info(heap_info_t *info);

/**
 * @brief Prüfen ob Heap-Warnung aktiv ist
 * @return true wenn Warnung, false sonst
 */
bool heap_monitor_has_warning(void);

/**
 * @brief Prüfen ob Heap kritisch ist
 * @return true wenn kritisch, false sonst
 */
bool heap_monitor_is_critical(void);

/**
 * @brief Heap-Monitor deinitialisieren (Mutex cleanup)
 */
void heap_monitor_deinit(void);

#endif // HEAP_MONITOR_H
