#include "servo.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <string.h>

static const char *TAG = "servo";

static bool valve_open = false;
static SemaphoreHandle_t servo_mutex = NULL;
static portMUX_TYPE servo_mux = portMUX_INITIALIZER_UNLOCKED;  // Critical Section für emergency_close

// Öffnungsereignisse Ringbuffer (SERVO_EVENT_MAX_COUNT in servo.h definiert)
static servo_event_t servo_events[SERVO_EVENT_MAX_COUNT];
static uint16_t servo_event_index = 0;
static uint16_t servo_event_count = 0;
static uint64_t valve_open_time = 0;  // Zeitpunkt wann Ventil geöffnet wurde

esp_err_t servo_init(void)
{
    esp_err_t ret;
    
    // Mutex erstellen
    servo_mutex = xSemaphoreCreateMutex();
    if (servo_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // LEDC Timer konfigurieren (14-Bit)
    ledc_timer_config_t timer_conf = {
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC Timer-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // LEDC Channel konfigurieren
    ledc_channel_config_t channel_conf = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = GPIO_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0
    };
    
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC Channel-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // GPIO5 (FET Enable) als Output konfigurieren
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_SERVO_ENABLE),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FET-Enable GPIO-Konfiguration fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // FET initial ausschalten (Servo stromlos)
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    ESP_LOGI(TAG, "Servo-Modul initialisiert (GPIO %d, FET-Enable GPIO %d) - FET stromlos", GPIO_SERVO, GPIO_SERVO_ENABLE);
    return ESP_OK;
}

void servo_calibrate(void)
{
    ESP_LOGI(TAG, "Servo-Kalibrierung gestartet");

    // FET einschalten
    gpio_set_level(GPIO_SERVO_ENABLE, 1);

    // Grundposition (geschlossen) anfahren
    ESP_LOGI(TAG, "Kalibrierung: Grundposition (geschlossen) anfahren");
    servo_set_position(g_servo_config.servo_close_us);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Sekunde halten

    // 100µs in Richtung offen anfahren
    uint32_t test_position = g_servo_config.servo_close_us - 100;
    ESP_LOGI(TAG, "Kalibrierung: 100µs in Richtung offen anfahren (%lu us)", test_position);
    servo_set_position(test_position);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Sekunde halten

    // Wieder auf Grundposition zurückfahren
    ESP_LOGI(TAG, "Kalibrierung: Zurück auf Grundposition");
    servo_set_position(g_servo_config.servo_close_us);
    vTaskDelay(pdMS_TO_TICKS(g_servo_config.fet_on_time_ms));

    // FET ausschalten
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    ESP_LOGI(TAG, "Servo-Kalibrierung abgeschlossen");
}

void servo_set_position(uint32_t pulse_us)
{
    // Pulsweite in 14-bit Duty umrechnen (20ms Periode, 16383 Schritte)
    uint32_t duty = (pulse_us * 16383) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGD(TAG, "Servo Position: %lu us", pulse_us);
}

void servo_open_valve(void)
{
    xSemaphoreTake(servo_mutex, portMAX_DELAY);

    if (valve_open) {
        xSemaphoreGive(servo_mutex);
        ESP_LOGW(TAG, "Wasserhahn bereits geöffnet");
        return;
    }

    valve_open = true;
    valve_open_time = esp_timer_get_time();
    xSemaphoreGive(servo_mutex);

    // FET einschalten (Servo-Stromversorgung aktivieren)
    gpio_set_level(GPIO_SERVO_ENABLE, 1);

    ESP_LOGI(TAG, "Wasserhahn öffnen");
    servo_set_position(g_servo_config.servo_open_us);

    // FET nach konfigurierbarer Zeit ausschalten (Strom sparen)
    vTaskDelay(pdMS_TO_TICKS(g_servo_config.fet_on_time_ms));
    gpio_set_level(GPIO_SERVO_ENABLE, 0);
}

void servo_close_valve(void)
{
    xSemaphoreTake(servo_mutex, portMAX_DELAY);

    if (!valve_open) {
        xSemaphoreGive(servo_mutex);
        return;
    }

    // Öffnungsdauer protokollieren
    uint64_t close_time = esp_timer_get_time();
    uint64_t duration_ms = (close_time - valve_open_time) / 1000ULL;

    valve_open = false;
    xSemaphoreGive(servo_mutex);

    // Ereignis hinzufügen (außerhalb des Mutex)
    if (duration_ms >= 1000) {  // Nur Ereignisse >= 1 Sekunde protokollieren
        uint64_t timestamp_ms = close_time / 1000;

        xSemaphoreTake(servo_mutex, portMAX_DELAY);
        servo_event_t *event = &servo_events[servo_event_index];
        event->timestamp_ms = timestamp_ms;
        event->duration_ms = duration_ms;

        servo_event_index = (servo_event_index + 1) % SERVO_EVENT_MAX_COUNT;
        if (servo_event_count < SERVO_EVENT_MAX_COUNT) {
            servo_event_count++;
        }
        xSemaphoreGive(servo_mutex);
    }

    ESP_LOGI(TAG, "Wasserhahn schließen (Dauer: %llu ms)", (unsigned long long)duration_ms);

    // FET einschalten, Servo positionieren, Timeout, FET aus
    gpio_set_level(GPIO_SERVO_ENABLE, 1);
    servo_set_position(g_servo_config.servo_close_us);
    vTaskDelay(pdMS_TO_TICKS(g_servo_config.fet_on_time_ms));
    gpio_set_level(GPIO_SERVO_ENABLE, 0);
}

bool servo_is_valve_open(void)
{
    bool open;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    open = valve_open;
    xSemaphoreGive(servo_mutex);
    return open;
}

void servo_emergency_close(void)
{
    // Critical Section für kritische Situationen (Race-Condition vermeiden)
    portENTER_CRITICAL(&servo_mux);
    valve_open = false;
    portEXIT_CRITICAL(&servo_mux);

    // FET einschalten, Servo positionieren, Timeout, FET aus
    gpio_set_level(GPIO_SERVO_ENABLE, 1);
    servo_set_position(g_servo_config.servo_close_us);
    vTaskDelay(pdMS_TO_TICKS(g_servo_config.fet_on_time_ms));
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    ESP_LOGE(TAG, "EMERGENCY: Wasserhahn geschlossen");
}

uint16_t servo_get_events(servo_event_t *events, uint16_t max_count)
{
    if (events == NULL || max_count == 0) {
        return 0;
    }

    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    uint16_t count = (servo_event_count < max_count) ? servo_event_count : max_count;

    // Ereignisse kopieren (älteste zuerst)
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (servo_event_index - servo_event_count + i + SERVO_EVENT_MAX_COUNT) % SERVO_EVENT_MAX_COUNT;
        events[i] = servo_events[idx];
    }

    xSemaphoreGive(servo_mutex);
    return count;
}

void servo_clear_events(void)
{
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    servo_event_index = 0;
    servo_event_count = 0;
    memset(servo_events, 0, sizeof(servo_events));
    xSemaphoreGive(servo_mutex);
}

void servo_deinit(void)
{
    // Mutex löschen
    if (servo_mutex != NULL) {
        vSemaphoreDelete(servo_mutex);
        servo_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Servo-Modul deinitialisiert");
}
