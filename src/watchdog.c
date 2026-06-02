#include "watchdog.h"
#include "config.h"
#include "error_log.h"
#include "servo.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watchdog";

static bool watchdog_active = false;

esp_err_t watchdog_init(void)
{
    esp_err_t ret;
    
    // Task Watchdog initialisieren
    ret = esp_task_wdt_init(TWDT_TIMEOUT_MS / 1000, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Watchdog initialisiert (Timeout: %d ms)", TWDT_TIMEOUT_MS);
    return ESP_OK;
}

static void watchdog_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Watchdog-Task gestartet (Core %d)", xPortGetCoreID());
    
    // Haupt-Task zum Watchdog hinzufügen
    esp_err_t ret = esp_task_wdt_add(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Task zum Watchdog hinzufügen fehlgeschlagen: %s", esp_err_to_name(ret));
        return;
    }
    
    watchdog_active = true;
    
    while (1) {
        // Watchdog reset
        esp_task_wdt_reset();
        
        vTaskDelay(pdMS_TO_TICKS(TWDT_TIMEOUT_MS / 2));  // Hälfte des Timeouts
    }
}

esp_err_t watchdog_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        watchdog_task,
        "watchdog",
        TWDT_TASK_STACK_SIZE,
        NULL,
        1,
        NULL,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Watchdog-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

void watchdog_reset(void)
{
    esp_task_wdt_reset();
}

bool watchdog_is_active(void)
{
    return watchdog_active;
}

// Watchdog-Callback für kritische Fehler
void esp_task_wdt_isr_user_handler(void)
{
    // Kritischer Fehler - Servo emergency close
    #if SERVO_EMERGENCY_CLOSE
    servo_emergency_close();
    #endif
    
    // Fehler loggen
    error_log_add(ERR_WATCHDOG_TRIGGER, "WATCHDOG", 3);
    
    ESP_LOGE(TAG, "CRITICAL: Watchdog Trigger - Servo geschlossen");
}
