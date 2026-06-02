#include "battery.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static float battery_voltage = 0.0;
static uint8_t battery_percent = 0;
static SemaphoreHandle_t battery_mutex = NULL;

esp_err_t battery_init(void)
{
    esp_err_t ret;
    
    // Mutex erstellen
    battery_mutex = xSemaphoreCreateMutex();
    if (battery_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // ADC initialisieren
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    
    ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // ADC-Kanal konfigurieren
    adc_oneshot_chan_cfg_t config = {
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    ret = adc_oneshot_config_channel(adc_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC-Kanal-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Battery-Modul initialisiert");
    return ESP_OK;
}

static void battery_update(void)
{
    int adc_raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC-Lesefehler: %s", esp_err_to_name(ret));
        return;
    }
    
    // ADC-Wert in Spannung umrechnen (0-4095 → 0-3.3V)
    float adc_voltage = (adc_raw * 3.3f) / 4095.0f;
    
    // Mit Spannungsteiler korrigieren
    float battery_voltage_actual = adc_voltage * (BATTERY_DIVIDER_R1 + BATTERY_DIVIDER_R2) / BATTERY_DIVIDER_R2;
    
    // Zellspannung berechnen (2 Zellen parallel)
    float cell_voltage = battery_voltage_actual / BATTERY_CELLS;
    
    // Prozent berechnen
    uint8_t percent = 0;
    if (cell_voltage >= BATTERY_VOLTAGE_MAX) {
        percent = 100;
    } else if (cell_voltage <= BATTERY_VOLTAGE_MIN) {
        percent = 0;
    } else {
        percent = (uint8_t)((cell_voltage - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN) * 100);
    }
    
    xSemaphoreTake(battery_mutex, portMAX_DELAY);
    battery_voltage = battery_voltage_actual;
    battery_percent = percent;
    xSemaphoreGive(battery_mutex);
    
    ESP_LOGD(TAG, "Batterie: %.2fV (%.1fV/Zelle, %d%%)", battery_voltage_actual, cell_voltage, percent);
}

static void battery_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Battery-Monitor-Task gestartet (Core %d)", xPortGetCoreID());
    
    while (1) {
        battery_update();
        vTaskDelay(pdMS_TO_TICKS(5000));  // Alle 5 Sekunden messen
    }
}

esp_err_t battery_start_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        battery_task,
        "battery_task",
        TASK_STACK_BATTERY,
        NULL,
        TASK_PRIO_BATTERY,
        NULL,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Battery-Monitor-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

float battery_get_voltage(void)
{
    float voltage;
    xSemaphoreTake(battery_mutex, portMAX_DELAY);
    voltage = battery_voltage;
    xSemaphoreGive(battery_mutex);
    return voltage;
}

uint8_t battery_get_percent(void)
{
    uint8_t percent;
    xSemaphoreTake(battery_mutex, portMAX_DELAY);
    percent = battery_percent;
    xSemaphoreGive(battery_mutex);
    return percent;
}

bool battery_is_critical(void)
{
    float voltage;
    xSemaphoreTake(battery_mutex, portMAX_DELAY);
    voltage = battery_voltage;
    xSemaphoreGive(battery_mutex);
    
    float cell_voltage = voltage / BATTERY_CELLS;
    return (cell_voltage < BATTERY_CRITICAL_VOLTAGE);
}
