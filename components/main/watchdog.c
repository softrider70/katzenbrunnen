#include "watchdog.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watchdog";

static bool watchdog_active = false;

esp_err_t watchdog_init(void)
{
    // TWDT-Konfiguration (IDF v5 API)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = TWDT_TIMEOUT_MS,
        .idle_core_mask = 0,        // Idle-Tasks nicht überwachen
        .trigger_panic = false,     // Panic deaktiviert für Debugging (Bootloop-Diagnose)
    };
    
    // Der TWDT ist je nach sdkconfig bereits initialisiert -> rekonfigurieren
    esp_err_t ret = esp_task_wdt_init(&twdt_config);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = esp_task_wdt_reconfigure(&twdt_config);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    watchdog_active = true;
    ESP_LOGI(TAG, "Watchdog initialisiert (Timeout: %d ms)", TWDT_TIMEOUT_MS);
    return ESP_OK;
}

esp_err_t watchdog_start_task(void)
{
    // Kein eigener Task mehr: die zu überwachenden Tasks melden sich selbst an
    // (watchdog_subscribe) und füttern den WDT (watchdog_feed).
    return ESP_OK;
}

esp_err_t watchdog_subscribe(void)
{
    esp_err_t ret = esp_task_wdt_add(NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_ARG) {
        ESP_LOGE(TAG, "Task-Anmeldung am Watchdog fehlgeschlagen: %s", esp_err_to_name(ret));
    }
    return ret;
}

void watchdog_stop(void)
{
    // Aktuellen Task vor Sleep vom TWDT abmelden (verhindert Timeout während Sleep)
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }
}

void watchdog_start(void)
{
    // Aktuellen Task nach Sleep wieder am TWDT anmelden
    esp_err_t ret = esp_task_wdt_add(NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_ARG) {
        ESP_LOGE(TAG, "Watchdog-Wiederanmeldung fehlgeschlagen: %s", esp_err_to_name(ret));
    }
}

void watchdog_feed(void)
{
    // Nur füttern wenn der aktuelle Task angemeldet ist
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
}

void watchdog_reset(void)
{
    watchdog_feed();
}

bool watchdog_is_active(void)
{
    return watchdog_active;
}

// Watchdog-ISR-Callback (weak override). MUSS ISR-sicher sein:
// keine Logging-/LEDC-/Mutex-Aufrufe. Servo-GPIO direkt LOW ziehen.
void esp_task_wdt_isr_user_handler(void)
{
    // Servo-Signal direkt deaktivieren (ISR-sicherer GPIO-Zugriff).
    // Nach dem folgenden Panic-Reset setzt servo_init() die Ruhelage.
    #if SERVO_EMERGENCY_CLOSE
    gpio_set_level(GPIO_SERVO, 0);
    #endif
    
    // ISR-sichere Ausgabe via ROM-printf
    esp_rom_printf("CRITICAL: Watchdog Trigger\n");
}
