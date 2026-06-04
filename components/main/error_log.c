#include "error_log.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "error_log";

static error_log_entry_t error_log[ERROR_LOG_MAX_ENTRIES];
static uint16_t error_log_count = 0;
static uint16_t error_log_index = 0;
static SemaphoreHandle_t error_log_mutex = NULL;

static const char* error_text_table[] = {
    [ERR_STACK_OVERFLOW] = "Stack Overflow",
    [ERR_WATCHDOG_TRIGGER] = "Watchdog Trigger",
    [ERR_SERVO_FAILURE] = "Servo Failure",
    [ERR_BATTERY_CRITICAL] = "Battery Critical",
    [ERR_PIR_FAILURE] = "PIR Failure",
    [ERR_ADC_FAILURE] = "ADC Failure",
    [ERR_NVS_FAILURE] = "NVS Failure",
    [ERR_WIFI_FAILURE] = "WiFi Failure",
    [ERR_OTA_FAILURE] = "OTA Failure",
    [ERR_MEMORY_ALLOC] = "Memory Allocation"
};

esp_err_t error_log_init(void)
{
    error_log_mutex = xSemaphoreCreateMutex();
    if (error_log_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    memset(error_log, 0, sizeof(error_log));
    error_log_count = 0;
    error_log_index = 0;
    
    ESP_LOGI(TAG, "Error-Log System initialisiert");
    return ESP_OK;
}

void error_log_generate_code(char *code, uint8_t error_id, uint64_t timestamp_ms)
{
    // Fehlercode-Format: E + 3-stellige ID + 4-stellige Zeit (Sekunden seit Boot mod 10000)
    uint32_t time_sec = (timestamp_ms / 1000) % 10000;
    snprintf(code, ERROR_CODE_LENGTH + 1, "E%03u%04" PRIu32, error_id, time_sec);
}

esp_err_t error_log_add(uint8_t error_id, const char *task_name, uint8_t severity)
{
    if (error_log_mutex == NULL) {
        return ESP_FAIL;
    }
    
    xSemaphoreTake(error_log_mutex, portMAX_DELAY);
    
    uint64_t timestamp_ms = esp_timer_get_time() / 1000;
    
    error_log_entry_t *entry = &error_log[error_log_index];
    
    error_log_generate_code(entry->code, error_id, timestamp_ms);
    entry->error_id = error_id;
    entry->timestamp_ms = timestamp_ms;
    entry->severity = severity;
    
    if (task_name != NULL) {
        strncpy(entry->task_name, task_name, sizeof(entry->task_name) - 1);
        entry->task_name[sizeof(entry->task_name) - 1] = '\0';
    } else {
        strncpy(entry->task_name, "SYSTEM", sizeof(entry->task_name) - 1);
    }
    
    // Index weiterzählen (Ringbuffer)
    error_log_index = (error_log_index + 1) % ERROR_LOG_MAX_ENTRIES;
    if (error_log_count < ERROR_LOG_MAX_ENTRIES) {
        error_log_count++;
    }
    
    xSemaphoreGive(error_log_mutex);
    
    const char *severity_str = (severity == 0) ? "INFO" : 
                              (severity == 1) ? "WARN" : 
                              (severity == 2) ? "ERROR" : "CRITICAL";
    
    ESP_LOGW(TAG, "Error logged: %s - %s (%s) - %s", 
             entry->code, error_log_get_text(error_id), severity_str, 
             task_name ? task_name : "SYSTEM");
    
    return ESP_OK;
}

const char* error_log_get_text(uint8_t error_id)
{
    if (error_id < sizeof(error_text_table) / sizeof(error_text_table[0])) {
        return error_text_table[error_id];
    }
    return "Unknown Error";
}

uint16_t error_log_get_all(error_log_entry_t *entries, uint16_t max_entries)
{
    if (error_log_mutex == NULL || entries == NULL) {
        return 0;
    }
    
    xSemaphoreTake(error_log_mutex, portMAX_DELAY);
    
    uint16_t count = (error_log_count < max_entries) ? error_log_count : max_entries;
    
    // Einträge in chronologischer Reihenfolge kopieren
    for (uint16_t i = 0; i < count; i++) {
        uint16_t index = (error_log_index - error_log_count + i + ERROR_LOG_MAX_ENTRIES) % ERROR_LOG_MAX_ENTRIES;
        entries[i] = error_log[index];
    }
    
    xSemaphoreGive(error_log_mutex);
    
    return count;
}

void error_log_clear(void)
{
    if (error_log_mutex == NULL) {
        return;
    }
    
    xSemaphoreTake(error_log_mutex, portMAX_DELAY);
    
    memset(error_log, 0, sizeof(error_log));
    error_log_count = 0;
    error_log_index = 0;
    
    xSemaphoreGive(error_log_mutex);
    
    ESP_LOGI(TAG, "Error-Log geleert");
}

void error_log_deinit(void)
{
    // Mutex löschen
    if (error_log_mutex != NULL) {
        vSemaphoreDelete(error_log_mutex);
        error_log_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Error-Log deinitialisiert");
}
