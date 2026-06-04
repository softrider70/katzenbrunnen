#include "pir.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pir";

// Spinlock schützt den 64-Bit-Zeitstempel vor Torn-Reads zwischen ISR und Task
static portMUX_TYPE pir_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t last_motion_time = 0;
static bool isr_service_installed = false;

// PIR-Bewegungserkennung im Erkennungsfenster (PIR_DETECTION_WINDOW_MS)
#define PIR_DETECTION_WINDOW_US (PIR_DETECTION_WINDOW_MS * 1000ULL)

// ISR: nur Zeitstempel setzen (ISR-sicher, kein Logging)
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    uint64_t t = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&pir_mux);
    last_motion_time = t;
    portEXIT_CRITICAL_ISR(&pir_mux);
}

esp_err_t pir_init(void)
{
    esp_err_t ret;
    
    // PIR Sensor GPIO konfigurieren (HIGH bei Bewegung -> beide Flanken erfassen)
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
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
    
    // ISR-Service installieren (global nur einmal)
    ret = gpio_install_isr_service(0);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        isr_service_installed = true;
    } else {
        ESP_LOGE(TAG, "ISR-Service-Installation fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gpio_isr_handler_add(GPIO_PIR_SENSOR, pir_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ISR-Handler-Registrierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "PIR-Modul initialisiert (GPIO %d)", GPIO_PIR_SENSOR);
    return ESP_OK;
}

esp_err_t pir_start_task(void)
{
    // Kein eigener Task nötig: Bewegungsauswertung erfolgt im control_task.
    return ESP_OK;
}

bool pir_motion_detected(void)
{
    uint64_t current_time = esp_timer_get_time();
    uint64_t last_motion = pir_get_last_motion_time();
    
    if (last_motion == 0) {
        return false;
    }
    // Bewegung erkannt, wenn letzte Bewegung innerhalb des Fensters liegt
    return ((current_time - last_motion) < PIR_DETECTION_WINDOW_US);
}

uint64_t pir_get_last_motion_time(void)
{
    uint64_t t;
    portENTER_CRITICAL(&pir_mux);
    t = last_motion_time;
    portEXIT_CRITICAL(&pir_mux);
    return t;
}
