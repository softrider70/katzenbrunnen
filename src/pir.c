#include "pir.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

static const char *TAG = "pir";

static volatile bool pir_cooldown = false;
static uint64_t motion_start_time = 0;
static uint64_t last_motion_time = 0;
static SemaphoreHandle_t pir_mutex = NULL;

static void pir_cooldown_callback(TimerHandle_t xTimer)
{
    xSemaphoreTake(pir_mutex, portMAX_DELAY);
    pir_cooldown = false;
    motion_start_time = 0;
    xSemaphoreGive(pir_mutex);
    ESP_LOGI(TAG, "PIR Cooldown beendet");
}

static void pir_isr_handler(void *arg)
{
    uint64_t current_time = esp_timer_get_time();
    
    // ISR-sichere Prüfung ohne Mutex (nur Flags setzen)
    if (!pir_cooldown) {
        if (motion_start_time == 0) {
            // Erste Bewegung erkannt
            motion_start_time = current_time;
            ESP_LOGI(TAG, "PIR Bewegung erkannt - Start Timer");
        } else {
            // Bewegung anhält
            last_motion_time = current_time;
            ESP_LOGD(TAG, "PIR Bewegung anhält");
        }
    }
}

static void pir_task(void *pvParameters)
{
    ESP_LOGI(TAG, "PIR-Task gestartet (Core %d)", xPortGetCoreID());
    
    // PIR Interrupt installieren
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_PIR_SENSOR, pir_isr_handler, NULL);
    
    while (1) {
        uint64_t current_time = esp_timer_get_time();
        
        xSemaphoreTake(pir_mutex, portMAX_DELAY);
        
        // Prüfen ob minimale Bewegungsdauer erreicht
        if (motion_start_time != 0) {
            uint64_t motion_duration = current_time - motion_start_time;
            if (motion_duration > (MIN_MOTION_DURATION_MS * 1000)) {
                ESP_LOGI(TAG, "Minimale Bewegungsdauer erreicht (%llu ms)", motion_duration / 1000);
                pir_cooldown = true;
                motion_start_time = 0;
                
                // Cooldown Timer starten
                TimerHandle_t cooldown_timer = xTimerCreate(
                    "pir_cooldown",
                    pdMS_TO_TICKS(PIR_COOLDOWN_MS),
                    pdFALSE,
                    NULL,
                    pir_cooldown_callback
                );
                xTimerStart(cooldown_timer, 0);
            }
        }
        
        xSemaphoreGive(pir_mutex);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t pir_init(void)
{
    esp_err_t ret;
    
    // Mutex erstellen
    pir_mutex = xSemaphoreCreateMutex();
    if (pir_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // PIR Sensor GPIO konfigurieren
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_PIR_SENSOR),
        .pull_down_en = 1,
        .pull_up_en = 0,
    };
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PIR GPIO-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "PIR-Modul initialisiert (GPIO %d)", GPIO_PIR_SENSOR);
    return ESP_OK;
}

esp_err_t pir_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        pir_task,
        "pir_task",
        TASK_STACK_PIR,
        NULL,
        TASK_PRIO_PIR,
        NULL,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des PIR-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

bool pir_motion_detected(void)
{
    uint64_t current_time = esp_timer_get_time();
    uint64_t last_motion;
    
    xSemaphoreTake(pir_mutex, portMAX_DELAY);
    last_motion = last_motion_time;
    xSemaphoreGive(pir_mutex);
    
    // Bewegung als erkannt wenn letzte Bewegung < 1 Sekunde her
    return ((current_time - last_motion) < 1000000);
}

uint64_t pir_get_last_motion_time(void)
{
    uint64_t time;
    xSemaphoreTake(pir_mutex, portMAX_DELAY);
    time = last_motion_time;
    xSemaphoreGive(pir_mutex);
    return time;
}

bool pir_is_cooldown(void)
{
    bool cooldown;
    xSemaphoreTake(pir_mutex, portMAX_DELAY);
    cooldown = pir_cooldown;
    xSemaphoreGive(pir_mutex);
    return cooldown;
}
