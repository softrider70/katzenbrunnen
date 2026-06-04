#include "stack_monitor.h"
#include "config.h"
#include "error_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "stack_monitor";

static stack_info_t stack_info[16];  // Max 16 Tasks
static uint8_t stack_info_count = 0;
static SemaphoreHandle_t stack_mutex = NULL;

esp_err_t stack_monitor_init(void)
{
    stack_mutex = xSemaphoreCreateMutex();
    if (stack_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    memset(stack_info, 0, sizeof(stack_info));
    stack_info_count = 0;
    
    ESP_LOGI(TAG, "Stack-Monitor initialisiert (ESP-IDF 6.1 kompatibel)");
    return ESP_OK;
}

static void update_stack_info(void)
{
    xSemaphoreTake(stack_mutex, portMAX_DELAY);
    
    // Vereinfachte Stack-Überwachung für ESP-IDF 6.1 (uxTaskGetSystemState nicht verfügbar)
    // Nur HighWaterMark für bekannte Tasks überwachen
    const char *known_tasks[] = {"katzenbrunnen", "control_task", "app_task", "wifi_task", "ota_task", "httpd"};
    stack_info_count = 0;
    
    for (size_t i = 0; i < sizeof(known_tasks) / sizeof(known_tasks[0]); i++) {
        TaskHandle_t task = xTaskGetHandle(known_tasks[i]);
        if (task != NULL) {
            UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(task);
            uint32_t free_bytes = high_water_mark * sizeof(StackType_t);
            
            strncpy(stack_info[stack_info_count].task_name, known_tasks[i], sizeof(stack_info[stack_info_count].task_name) - 1);
            stack_info[stack_info_count].task_name[sizeof(stack_info[stack_info_count].task_name) - 1] = '\0';
            stack_info[stack_info_count].stack_free = free_bytes;
            stack_info[stack_info_count].stack_size = 0;  // Nicht ermittelbar ohne uxTaskGetSystemState
            stack_info[stack_info_count].stack_percent = 0;
            
            // Warnung/Kritisch anhand absolutem freiem Stack-Reservepuffer
            stack_info[stack_info_count].warning  = (free_bytes < STACK_FREE_WARNING_BYTES);
            stack_info[stack_info_count].critical = (free_bytes < STACK_FREE_CRITICAL_BYTES);
            
            // Fehler loggen bei niedrigem freiem Stack
            if (stack_info[stack_info_count].critical) {
                error_log_add(ERR_STACK_OVERFLOW, stack_info[stack_info_count].task_name, 3);
            } else if (stack_info[stack_info_count].warning) {
                error_log_add(ERR_STACK_OVERFLOW, stack_info[stack_info_count].task_name, 1);
            }
            
            stack_info_count++;
            if (stack_info_count >= 16) break;
        }
    }
    
    xSemaphoreGive(stack_mutex);
}

static void stack_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Stack-Monitor-Task gestartet (Core %d)", xPortGetCoreID());
    
    while (1) {
        update_stack_info();
        vTaskDelay(pdMS_TO_TICKS(STACK_MONITOR_INTERVAL_MS));
    }
}

esp_err_t stack_monitor_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        stack_monitor_task,
        "stack_monitor",
        2048,
        NULL,
        2,
        NULL,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Stack-Monitor-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

uint8_t stack_monitor_get_all(stack_info_t *info, uint8_t max_tasks)
{
    if (stack_mutex == NULL || info == NULL) {
        return 0;
    }
    
    xSemaphoreTake(stack_mutex, portMAX_DELAY);
    
    uint8_t count = (stack_info_count < max_tasks) ? stack_info_count : max_tasks;
    memcpy(info, stack_info, count * sizeof(stack_info_t));
    
    xSemaphoreGive(stack_mutex);
    
    return count;
}

bool stack_monitor_has_warning(const char *task_name)
{
    if (stack_mutex == NULL || task_name == NULL) {
        return false;
    }
    
    xSemaphoreTake(stack_mutex, portMAX_DELAY);
    
    bool has_warning = false;
    for (uint8_t i = 0; i < stack_info_count; i++) {
        if (strcmp(stack_info[i].task_name, task_name) == 0) {
            has_warning = stack_info[i].warning;
            break;
        }
    }
    
    xSemaphoreGive(stack_mutex);
    
    return has_warning;
}

void stack_monitor_deinit(void)
{
    // Mutex löschen
    if (stack_mutex != NULL) {
        vSemaphoreDelete(stack_mutex);
        stack_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Stack-Monitor deinitialisiert");
}
