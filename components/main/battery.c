#include "battery.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;
static float battery_voltage = 0.0;
static uint8_t battery_percent = 0;
static SemaphoreHandle_t battery_mutex = NULL;

// ADC-Kalibrierung initialisieren (Curve-Fitting auf ESP32-S3)
static bool battery_adc_calibration_init(void)
{
    esp_err_t ret;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC-Kalibrierung: Curve Fitting");
        return true;
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&line_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC-Kalibrierung: Line Fitting");
        return true;
    }
#endif
    ESP_LOGW(TAG, "ADC-Kalibrierung nicht verfügbar - nutze Rohwert-Schätzung");
    return false;
}

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
    
    // ADC-Kanal konfigurieren (Kanal wird separat übergeben)
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    ret = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC-Kanal-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Kalibrierung initialisieren (optional)
    adc_calibrated = battery_adc_calibration_init();
    
    ESP_LOGI(TAG, "Battery-Modul initialisiert");
    return ESP_OK;
}

static void battery_update(void)
{
    // Mehrfachmessung zur Rauschunterdrückung
    int adc_raw_sum = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        int adc_raw = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC-Lesefehler: %s", esp_err_to_name(ret));
            return;
        }
        adc_raw_sum += adc_raw;
    }
    int adc_raw = adc_raw_sum / samples;
    
    // ADC-Wert in Spannung (mV) umrechnen - kalibriert wenn verfügbar
    float adc_voltage;  // in Volt
    if (adc_calibrated) {
        int voltage_mv = 0;
        if (adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv) == ESP_OK) {
            adc_voltage = voltage_mv / 1000.0f;
        } else {
            adc_voltage = (adc_raw * 3.1f) / 4095.0f;
        }
    } else {
        // Grobe Schätzung ohne Kalibrierung (12 dB ~ 3.1V Vollausschlag)
        adc_voltage = (adc_raw * 3.1f) / 4095.0f;
    }
    
    // Mit Spannungsteiler auf Packspannung hochrechnen
    float battery_voltage_actual = adc_voltage * (BATTERY_DIVIDER_R1 + BATTERY_DIVIDER_R2) / BATTERY_DIVIDER_R2;
    
    // Zellspannung berechnen (Zellen in Serie)
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
