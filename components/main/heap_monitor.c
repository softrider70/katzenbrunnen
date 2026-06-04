#include "heap_monitor.h"
#include "config.h"
#include "error_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "heap_monitor";

static heap_info_t heap_info;
static SemaphoreHandle_t heap_mutex = NULL;
static bool critical_logged = false;
static bool warning_logged = false;

esp_err_t heap_monitor_init(void)
{
    heap_mutex = xSemaphoreCreateMutex();
    if (heap_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    memset(&heap_info, 0, sizeof(heap_info));
    
    ESP_LOGI(TAG, "Heap-Monitor initialisiert");
    return ESP_OK;
}

static void update_heap_info(void)
{
    xSemaphoreTake(heap_mutex, portMAX_DELAY);
    
    // Heap-Informationen abrufen
    heap_info.total_heap = esp_get_total_heap_size();
    heap_info.free_heap = esp_get_free_heap_size();
    heap_info.min_free_heap = esp_get_minimum_free_heap_size();
    heap_info.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    
    // Freier Heap in Prozent berechnen
    if (heap_info.total_heap > 0) {
        heap_info.free_percent = (uint8_t)((heap_info.free_heap * 100) / heap_info.total_heap);
    } else {
        heap_info.free_percent = 0;
    }
    
    // Warnung/Kritisch anhand freiem Heap
    heap_info.warning = (heap_info.free_heap < HEAP_FREE_WARNING_BYTES);
    heap_info.critical = (heap_info.free_heap < HEAP_FREE_CRITICAL_BYTES);
    
    // Fehler loggen bei niedrigem freiem Heap (nur einmal pro Zustand)
    if (heap_info.critical && !critical_logged) {
        error_log_add(ERR_HEAP_LOW, "heap_monitor", 3);
        ESP_LOGE(TAG, "Kritischer Heap: %lu bytes frei (%u%%)", heap_info.free_heap, heap_info.free_percent);
        critical_logged = true;
        warning_logged = false;
    } else if (heap_info.warning && !warning_logged && !critical_logged) {
        error_log_add(ERR_HEAP_LOW, "heap_monitor", 1);
        ESP_LOGW(TAG, "Heap-Warnung: %lu bytes frei (%u%%)", heap_info.free_heap, heap_info.free_percent);
        warning_logged = true;
    } else if (!heap_info.warning && !heap_info.critical) {
        // Zustände zurücksetzen wenn Heap wieder normal
        critical_logged = false;
        warning_logged = false;
    }
    
    xSemaphoreGive(heap_mutex);
}

static void heap_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heap-Monitor-Task gestartet (Core %d)", xPortGetCoreID());
    
    while (1) {
        update_heap_info();
        vTaskDelay(pdMS_TO_TICKS(HEAP_MONITOR_INTERVAL_MS));
    }
}

esp_err_t heap_monitor_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        heap_monitor_task,
        "heap_monitor",
        2048,
        NULL,
        2,
        NULL,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Heap-Monitor-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t heap_monitor_get_info(heap_info_t *info)
{
    if (heap_mutex == NULL || info == NULL) {
        return ESP_FAIL;
    }
    
    xSemaphoreTake(heap_mutex, portMAX_DELAY);
    memcpy(info, &heap_info, sizeof(heap_info_t));
    xSemaphoreGive(heap_mutex);
    
    return ESP_OK;
}

bool heap_monitor_has_warning(void)
{
    if (heap_mutex == NULL) {
        return false;
    }
    
    xSemaphoreTake(heap_mutex, portMAX_DELAY);
    bool has_warning = heap_info.warning;
    xSemaphoreGive(heap_mutex);
    
    return has_warning;
}

bool heap_monitor_is_critical(void)
{
    if (heap_mutex == NULL) {
        return false;
    }
    
    xSemaphoreTake(heap_mutex, portMAX_DELAY);
    bool is_critical = heap_info.critical;
    xSemaphoreGive(heap_mutex);
    
    return is_critical;
}

void heap_monitor_deinit(void)
{
    // Mutex löschen
    if (heap_mutex != NULL) {
        vSemaphoreDelete(heap_mutex);
        heap_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Heap-Monitor deinitialisiert");
}
