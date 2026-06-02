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
    
    ESP_LOGI(TAG, "Stack-Monitor initialisiert");
    return ESP_OK;
}

static void update_stack_info(void)
{
    xSemaphoreTake(stack_mutex, portMAX_DELAY);
    
    // Task-Liste abrufen
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_status_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    
    if (task_status_array != NULL) {
        task_count = uxTaskGetSystemState(task_status_array, task_count, NULL);
        
        stack_info_count = (task_count < 16) ? task_count : 16;
        
        for (UBaseType_t i = 0; i < stack_info_count; i++) {
            strncpy(stack_info[i].task_name, task_status_array[i].pcTaskName, sizeof(stack_info[i].task_name) - 1);
            stack_info[i].task_name[sizeof(stack_info[i].task_name) - 1] = '\0';
            
            stack_info[i].stack_size = task_status_array[i].usStackHighWaterMark;
            stack_info[i].stack_free = task_status_array[i].usStackHighWaterMark;
            
            // Stack-Nutzung in Prozent berechnen
            uint32_t total_stack = task_status_array[i].uxTaskStackSize;
            if (total_stack > 0) {
                stack_info[i].stack_percent = (uint8_t)(((total_stack - stack_info[i].stack_free) * 100) / total_stack);
            } else {
                stack_info[i].stack_percent = 0;
            }
            
            stack_info[i].warning = (stack_info[i].stack_percent > STACK_WARNING_PERCENT);
            stack_info[i].critical = (stack_info[i].stack_percent > STACK_CRITICAL_PERCENT);
            
            // Fehler loggen bei kritischer Stack-Nutzung
            if (stack_info[i].critical) {
                error_log_add(ERR_STACK_OVERFLOW, stack_info[i].task_name, 3);
            } else if (stack_info[i].warning) {
                error_log_add(ERR_STACK_OVERFLOW, stack_info[i].task_name, 1);
            }
        }
        
        vPortFree(task_status_array);
    } else {
        ESP_LOGE(TAG, "Memory Allocation fehlgeschlagen für Stack-Monitor");
        error_log_add(ERR_MEMORY_ALLOC, "stack_monitor", 2);
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
