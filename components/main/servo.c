#include "servo.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "servo";

static bool valve_open = false;
static SemaphoreHandle_t servo_mutex = NULL;

esp_err_t servo_init(void)
{
    esp_err_t ret;
    
    // Mutex erstellen
    servo_mutex = xSemaphoreCreateMutex();
    if (servo_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // LEDC Timer konfigurieren
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
    
    // Servo auf geschlossene Position setzen
    servo_set_position(SERVO_CLOSE_ANGLE_US);
    
    ESP_LOGI(TAG, "Servo-Modul initialisiert (GPIO %d)", GPIO_SERVO);
    return ESP_OK;
}

void servo_set_position(uint32_t pulse_us)
{
    // Pulsweite in 16-bit Duty umrechnen (20ms Periode)
    uint32_t duty = (pulse_us * 65535) / 20000;
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
    xSemaphoreGive(servo_mutex);
    
    ESP_LOGI(TAG, "Wasserhahn öffnen");
    servo_set_position(SERVO_OPEN_ANGLE_US);
}

void servo_close_valve(void)
{
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    
    if (!valve_open) {
        xSemaphoreGive(servo_mutex);
        return;
    }
    
    valve_open = false;
    xSemaphoreGive(servo_mutex);
    
    ESP_LOGI(TAG, "Wasserhahn schließen");
    servo_set_position(SERVO_CLOSE_ANGLE_US);
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
    // Direkter Aufruf ohne Mutex für kritische Situationen
    valve_open = false;
    servo_set_position(SERVO_CLOSE_ANGLE_US);
    ESP_LOGE(TAG, "EMERGENCY: Wasserhahn geschlossen");
}
