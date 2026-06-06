#include "pir.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "pir";

// Spinlock schützt den 64-Bit-Zeitstempel vor Torn-Reads zwischen ISR und Task
static portMUX_TYPE pir_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t last_motion_time = 0;
static bool isr_service_installed = false;

// Bewegungsereignisse Ringbuffer (PIR_EVENT_MAX_COUNT in pir.h definiert)
static pir_event_t pir_events[PIR_EVENT_MAX_COUNT];
static uint16_t pir_event_index = 0;
static uint16_t pir_event_count = 0;

// PIR-Bewegungserkennungsfenster (für pulsierendes PIR-Signal)
#define PIR_DETECTION_WINDOW_US 30000000ULL  // 30 Sekunden

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
    
    // PIR Sensor GPIO konfigurieren (HIGH bei Bewegung -> nur steigende Flanke erfassen)
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

bool pir_get_gpio_level(void)
{
    return gpio_get_level(GPIO_PIR_SENSOR) == 1;
}

uint64_t pir_get_last_motion_time(void)
{
    uint64_t t;
    portENTER_CRITICAL(&pir_mux);
    t = last_motion_time;
    portEXIT_CRITICAL(&pir_mux);
    return t;
}

/**
 * @brief Bewegungsereignis hinzufügen (wird von control_task aufgerufen)
 */
void pir_add_event(uint64_t duration_ms)
{
    uint64_t timestamp_ms = esp_timer_get_time() / 1000;

    portENTER_CRITICAL(&pir_mux);
    pir_event_t *event = &pir_events[pir_event_index];
    event->timestamp_ms = timestamp_ms;
    event->duration_ms = duration_ms;

    pir_event_index = (pir_event_index + 1) % PIR_EVENT_MAX_COUNT;
    if (pir_event_count < PIR_EVENT_MAX_COUNT) {
        pir_event_count++;
    }
    portEXIT_CRITICAL(&pir_mux);
}

uint16_t pir_get_events(pir_event_t *events, uint16_t max_count)
{
    if (events == NULL || max_count == 0) {
        return 0;
    }

    portENTER_CRITICAL(&pir_mux);
    uint16_t count = (pir_event_count < max_count) ? pir_event_count : max_count;

    // Ereignisse kopieren (älteste zuerst)
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (pir_event_index - pir_event_count + i + PIR_EVENT_MAX_COUNT) % PIR_EVENT_MAX_COUNT;
        events[i] = pir_events[idx];
    }

    portEXIT_CRITICAL(&pir_mux);
    return count;
}

void pir_clear_events(void)
{
    portENTER_CRITICAL(&pir_mux);
    pir_event_index = 0;
    pir_event_count = 0;
    memset(pir_events, 0, sizeof(pir_events));
    portEXIT_CRITICAL(&pir_mux);
}
