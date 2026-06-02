#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config.h"
#include "pir.h"
#include "servo.h"
#include "battery.h"
#include "error_log.h"
#include "stack_monitor.h"
#include "watchdog.h"

static const char *TAG = "katzenbrunnen";

// NVS handle für persistenten Speicher
nvs_handle_t nvs_handle;

// Status-Variablen
static bool water_flow_active = false;
static uint32_t activation_count = 0;
static uint64_t last_motion_time = 0;

// Synchronisation
static SemaphoreHandle_t state_mutex = NULL;

/**
 * @brief GPIO und Hardware initialisieren
 */
static esp_err_t init_hardware(void)
{
    esp_err_t ret = ESP_OK;
    
    // GPIO Konfiguration
    gpio_config_t io_conf = {};
    
    // Taster (Input mit Pull-up)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_BUTTON);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    
    // Mutex erstellen
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // Module initialisieren
    ret = error_log_init();
    if (ret != ESP_OK) return ret;
    
    ret = pir_init();
    if (ret != ESP_OK) return ret;
    
    ret = servo_init();
    if (ret != ESP_OK) return ret;
    
    ret = battery_init();
    if (ret != ESP_OK) return ret;
    
    ret = stack_monitor_init();
    if (ret != ESP_OK) return ret;
    
    ret = watchdog_init();
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "Hardware initialisiert");
    return ESP_OK;
}

/**
 * @brief NVS initialisieren und Daten laden
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Aktivierungszykler aus NVS laden
    ret = nvs_get_u32(nvs_handle, NVS_ACTIVATION_COUNT_KEY, &activation_count);
    if (ret != ESP_OK) {
        activation_count = 0;
        ESP_LOGI(TAG, "Keine Aktivierungszykler gefunden, starte bei 0");
    } else {
        ESP_LOGI(TAG, "Geladene Aktivierungszykler: %lu", activation_count);
    }
    
    return ESP_OK;
}

/**
 * @brief Aktivierungszykler in NVS speichern
 */
static void save_activation_count(void)
{
    esp_err_t ret = nvs_set_u32(nvs_handle, NVS_ACTIVATION_COUNT_KEY, activation_count);
    if (ret == ESP_OK) {
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Aktivierungszykler gespeichert: %lu", activation_count);
    } else {
        ESP_LOGE(TAG, "Fehler beim Speichern der Aktivierungszykler");
    }
}

/**
 * @brief Wasserhahn öffnen
 */
static void open_water_valve(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    
    if (water_flow_active) {
        xSemaphoreGive(state_mutex);
        ESP_LOGW(TAG, "Wasserhahn bereits geöffnet");
        return;
    }
    
    water_flow_active = true;
    activation_count++;
    last_motion_time = esp_timer_get_time();
    
    xSemaphoreGive(state_mutex);
    
    ESP_LOGI(TAG, "Wasserhahn geöffnet (Zyklus %lu)", activation_count);
    servo_open_valve();
    
    save_activation_count();
}

/**
 * @brief Wasserhahn schließen
 */
static void close_water_valve(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    
    if (!water_flow_active) {
        xSemaphoreGive(state_mutex);
        return;
    }
    
    water_flow_active = false;
    xSemaphoreGive(state_mutex);
    
    servo_close_valve();
    
    ESP_LOGI(TAG, "Wasserhahn geschlossen");
}

/**
 * @brief Steuerungs-Task - Wasserhahn-Logik
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Steuerungs-Task gestartet (Core %d)", xPortGetCoreID());
    
    while (1) {
        uint64_t current_time = esp_timer_get_time();
        
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        
        // Prüfen ob minimale Bewegungsdauer erreicht und Wasserhahn öffnen
        if (!water_flow_active && pir_motion_detected() && !pir_is_cooldown()) {
            uint64_t last_motion = pir_get_last_motion_time();
            uint64_t motion_duration = current_time - last_motion;
            
            if (motion_duration > (MIN_MOTION_DURATION_MS * 1000)) {
                ESP_LOGI(TAG, "Minimale Bewegungsdauer erreicht (%llu ms)", motion_duration / 1000);
                xSemaphoreGive(state_mutex);
                open_water_valve();
                xSemaphoreTake(state_mutex, portMAX_DELAY);
            }
        }
        
        // Timeout ohne Bewegung - Wasserhahn schließen
        if (water_flow_active) {
            if (pir_motion_detected()) {
                last_motion_time = current_time;
            }
            
            uint64_t no_motion_duration = current_time - last_motion_time;
            if (no_motion_duration > (MOTION_TIMEOUT_MS * 1000)) {
                ESP_LOGI(TAG, "Timeout ohne Bewegung (%llu ms) - Wasserhahn schließen", no_motion_duration / 1000);
                xSemaphoreGive(state_mutex);
                close_water_valve();
                xSemaphoreTake(state_mutex, portMAX_DELAY);
            }
        }
        
        xSemaphoreGive(state_mutex);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Hauptanwendungs-Task - Taster und Status-Logging
 */
static void app_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Hauptanwendungs-Task gestartet (Core %d)", xPortGetCoreID());
    
    while (1) {
        // Manueller Taster prüfen
        if (gpio_get_level(GPIO_BUTTON) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));  // Entprellung
            if (gpio_get_level(GPIO_BUTTON) == 0) {
                ESP_LOGI(TAG, "Manueller Taster gedrückt");
                if (!servo_is_valve_open()) {
                    open_water_valve();
                } else {
                    close_water_valve();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));  // Verhindere mehrfache Aktivierung
            }
        }
        
        // Status-Logging alle 30 Sekunden
        static uint32_t last_status = 0;
        uint32_t current_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_tick - last_status > 30000) {
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            bool flow_active = water_flow_active;
            uint32_t activations = activation_count;
            xSemaphoreGive(state_mutex);
            
            float batt_voltage = battery_get_voltage();
            uint8_t batt_percent = battery_get_percent();
            
            ESP_LOGI(TAG, "Status - Wasserfluss: %s, Zykler: %lu, Batterie: %.2fV (%d%%)", 
                     flow_active ? "AN" : "AUS", activations, batt_voltage, batt_percent);
            last_status = current_tick;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms Zyklus
    }
}

/**
 * @brief Anwendungseinstiegspunkt
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Katzenbrunnen ESP32-S3 gestartet");
    ESP_LOGI(TAG, "Version: %s", APP_VERSION);
    
    // Hardware initialisieren
    if (init_hardware() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware-Initialisierung fehlgeschlagen");
        return;
    }
    
    // NVS initialisieren
    if (init_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Initialisierung fehlgeschlagen");
        return;
    }
    
    // System-Info ausgeben
    ESP_LOGI(TAG, "Chip-Revision: %d", esp_chip_revision());
    ESP_LOGI(TAG, "Freier Heap: %u bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimal freier Heap: %u bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "ESP32-S3 erkannt: %s", 
             strcmp(CONFIG_IDF_TARGET, "esp32s3") == 0 ? "Ja" : "Nein");
    
    // Module-Tasks starten
    if (pir_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "PIR-Task Start fehlgeschlagen");
    }
    
    if (battery_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Battery-Task Start fehlgeschlagen");
    }
    
    if (stack_monitor_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Stack-Monitor-Task Start fehlgeschlagen");
    }
    
    if (watchdog_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog-Task Start fehlgeschlagen");
    }
    
    // Steuerungs-Task erstellen (Core 0)
    BaseType_t ret = xTaskCreatePinnedToCore(
        control_task,
        "control_task",
        TASK_STACK_SERVO,
        NULL,
        TASK_PRIO_SERVO,
        NULL,
        TASK_CORE_CONTROL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Steuerungs-Tasks");
    }
    
    // Hauptanwendungs-Task erstellen (Core 0)
    ret = xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        CONFIG_APP_STACK_SIZE,
        NULL,
        CONFIG_APP_PRIORITY,
        NULL,
        TASK_CORE_CONTROL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Application Tasks");
    } else {
        ESP_LOGI(TAG, "Katzenbrunnen erfolgreich initialisiert");
        ESP_LOGI(TAG, "Task-Architektur: PIR/Battery/Stack/WDT/Control/App auf Core 0");
    }
}
