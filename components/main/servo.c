#include "servo.h"
#include "config.h"
#include "watchdog.h"
#include "telegram.h"
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
#include <time.h>

static const char *TAG = "servo";

static bool valve_open = false;
static SemaphoreHandle_t servo_mutex = NULL;
static portMUX_TYPE servo_mux = portMUX_INITIALIZER_UNLOCKED;  // Critical Section für emergency_close
static uint64_t valve_open_time = 0;  // Zeitpunkt wann Ventil geöffnet wurde (µs)
static time_t valve_open_local_time = 0;  // Lokale Zeit wann Ventil geöffnet wurde

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

/**
 * @brief Warten mit Watchdog-Feeding für FET-Aktivierung
 */
static void servo_wait_with_fet(uint32_t duration_ms)
{
    for (int i = 0; i < duration_ms / SERVO_FET_DELAY_MS; i++) {
        vTaskDelay(pdMS_TO_TICKS(SERVO_FET_DELAY_MS));
        watchdog_feed();
    }
    if (duration_ms % SERVO_FET_DELAY_MS != 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms % SERVO_FET_DELAY_MS));
        watchdog_feed();
    }
}

void servo_set_position(uint32_t pulse_us)
{
    // Pulsweite in 14-bit Duty umrechnen (20ms Periode, 16383 Schritte)
    uint32_t duty = (pulse_us * 16383) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGD(TAG, "Servo Position: %lu us", pulse_us);
}

void servo_calibrate(void)
{
    ESP_LOGI(TAG, "Servo-Kalibrierung gestartet");

    // FET einschalten
    gpio_set_level(GPIO_SERVO_ENABLE, 1);

    // Grundposition (geschlossen) anfahren
    ESP_LOGI(TAG, "Kalibrierung: Grundposition (geschlossen) anfahren");
    servo_set_position(g_servo_config.servo_close_us);
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);  // Watchdog-Feeding statt blockierendem Delay

    // Position geöffnet anfahren
    ESP_LOGI(TAG, "Kalibrierung: Position geöffnet anfahren (%lu us)", g_servo_config.servo_open_us);
    servo_set_position(g_servo_config.servo_open_us);
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);  // Watchdog-Feeding statt blockierendem Delay

    // Wieder auf Grundposition zurückfahren
    ESP_LOGI(TAG, "Kalibrierung: Zurück auf Grundposition");
    servo_set_position(g_servo_config.servo_close_us);
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);  // Watchdog-Feeding statt blockierendem Delay

    // FET ausschalten
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    ESP_LOGI(TAG, "Servo-Kalibrierung abgeschlossen");

    // Telegram-Startup-Nachricht senden
    if (telegram_is_configured()) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "🚀 Katzenbrunnen gestartet\n"
            "✅ Kalibrierung abgeschlossen\n"
            "📊 Freier Heap: %lu KB\n"
            "⚙️ Servo: %lu/%lu µs",
            (unsigned long)(esp_get_free_heap_size() / 1024),
            g_servo_config.servo_open_us,
            g_servo_config.servo_close_us);
        telegram_send_message(msg);
    }
}

void servo_set_position_with_fet(uint32_t pulse_us, uint32_t fet_duration_ms)
{
    // FET aktivieren
    gpio_set_level(GPIO_SERVO_ENABLE, 1);
    ESP_LOGI(TAG, "FET aktiviert für %lu ms", fet_duration_ms);

    // Servo-Position setzen
    servo_set_position(pulse_us);

    // Warten auf Servo-Stellzeit
    vTaskDelay(pdMS_TO_TICKS(fet_duration_ms));

    // FET deaktivieren
    gpio_set_level(GPIO_SERVO_ENABLE, 0);
    ESP_LOGI(TAG, "FET deaktiviert");
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

    // Verzögerung damit Servo-Stromversorgung stabilisiert
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Wasserhahn öffnen");
    servo_set_position(g_servo_config.servo_open_us);

    // Lokale Zeit speichern für Nacht-Buffer
    time(&valve_open_local_time);

    // Telegram-Nachricht senden (nicht puffern)
    if (telegram_is_configured() && !telegram_is_night_mode()) {
        telegram_send_message("💧 Wasserhahn geöffnet - Katze trinkt");
    }

    // FET nach konfigurierbarer Zeit ausschalten (Strom sparen)
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);
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

    ESP_LOGI(TAG, "Wasserhahn schließen (Dauer: %llu ms)", (unsigned long long)duration_ms);

    // FET einschalten, Servo positionieren, Timeout, FET aus
    gpio_set_level(GPIO_SERVO_ENABLE, 1);
    servo_set_position(g_servo_config.servo_close_us);
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    // Telegram-Nachricht senden oder puffern (Nacht-Modus)
    if (telegram_is_configured()) {
        if (telegram_is_night_mode()) {
            // Startzeit + Dauer im Format "hh:mi+ss" puffern
            struct tm timeinfo;
            localtime_r(&valve_open_local_time, &timeinfo);
            char event_text[32];
            snprintf(event_text, sizeof(event_text), "%02d:%02d+%llu", timeinfo.tm_hour, timeinfo.tm_min, (unsigned long long)(duration_ms / 1000));
            telegram_buffer_night_event(event_text);
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "🚰 Wasserhahn geschlossen - Dauer: %llu s", (unsigned long long)(duration_ms / 1000));
            telegram_send_message(msg);
        }
    }
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
    servo_wait_with_fet(g_servo_config.fet_on_time_ms);
    gpio_set_level(GPIO_SERVO_ENABLE, 0);

    ESP_LOGE(TAG, "EMERGENCY: Wasserhahn geschlossen");
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
