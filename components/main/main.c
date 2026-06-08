#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "config.h"
#include "pir.h"
#include "servo.h"
#include "error_log.h"
#include "stack_monitor.h"
#include "heap_monitor.h"
#include "watchdog.h"
#include "wifi.h"
#include "web_server.h"
#include "ota.h"
#include "led.h"
#include "telegram.h"

static const char *TAG = "katzenbrunnen";

// NVS handle für persistenten Speicher
nvs_handle_t g_nvs_handle;

// Servo Runtime-Konfiguration
servo_config_t g_servo_config = {
    .close_timeout_ms = CLOSE_TIMEOUT_MS,
    .servo_open_us = SERVO_OPEN_ANGLE_US,
    .servo_close_us = SERVO_CLOSE_ANGLE_US,
    .fet_on_time_ms = 5000
};

// SNTP Zeit-Synchronisation
static bool sntp_initialized = false;
static bool calibration_pending = false;
static int last_night_send_day = -1;  // Letzter Tag, an dem Nacht-Nachrichten gesendet wurden

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Zeit synchronisiert: %s", ctime(&tv->tv_sec));

    // Kalibrierung im control_task auslösen (nicht im Interrupt-Kontext)
    calibration_pending = true;
}

static void initialize_sntp(void)
{
    if (sntp_initialized) {
        return;
    }
    ESP_LOGI(TAG, "Initialisiere SNTP");
    
    // Zeitzone auf Berlin einstellen
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0", 1);
    tzset();
    
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    sntp_initialized = true;
}

/**
 * @brief Servo-Konfiguration aus NVS laden
 */
static void load_servo_config_from_nvs(void)
{
    esp_err_t ret;
    uint32_t value;

    // close_timeout_ms (1-30s = 1000-30000ms)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_CLOSE_TIMEOUT_MS, &value);
    if (ret == ESP_OK) {
        if (value >= 1000 && value <= 30000) {
            g_servo_config.close_timeout_ms = value;
            ESP_LOGI(TAG, "Close Timeout aus NVS: %lu ms", value);
        } else {
            ESP_LOGW(TAG, "Close Timeout aus NVS ungültig (%lu ms), verwende Default: %lu ms", value, g_servo_config.close_timeout_ms);
        }
    } else {
        ESP_LOGI(TAG, "Close Timeout nicht in NVS, verwende Default: %lu ms", g_servo_config.close_timeout_ms);
    }

    // servo_open_us (50-20000µs)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_SERVO_OPEN_US, &value);
    if (ret == ESP_OK) {
        if (value >= 50 && value <= 20000) {
            g_servo_config.servo_open_us = value;
            ESP_LOGI(TAG, "Servo Open aus NVS: %lu us", value);
        } else {
            ESP_LOGW(TAG, "Servo Open aus NVS ungültig (%lu us), verwende Default: %lu us", value, g_servo_config.servo_open_us);
        }
    } else {
        ESP_LOGI(TAG, "Servo Open nicht in NVS, verwende Default: %lu us", g_servo_config.servo_open_us);
    }

    // servo_close_us (50-20000µs)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_SERVO_CLOSE_US, &value);
    if (ret == ESP_OK) {
        if (value >= 50 && value <= 20000) {
            g_servo_config.servo_close_us = value;
            ESP_LOGI(TAG, "Servo Close aus NVS: %lu us", value);
        } else {
            ESP_LOGW(TAG, "Servo Close aus NVS ungültig (%lu us), verwende Default: %lu us", value, g_servo_config.servo_close_us);
        }
    } else {
        ESP_LOGI(TAG, "Servo Close nicht in NVS, verwende Default: %lu us", g_servo_config.servo_close_us);
    }

    // fet_on_time_ms (1-10s = 1000-10000ms)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_FET_ON_TIME_MS, &value);
    if (ret == ESP_OK) {
        if (value >= 1000 && value <= 10000) {
            g_servo_config.fet_on_time_ms = value;
            ESP_LOGI(TAG, "FET On Time aus NVS: %lu ms", value);
        } else {
            ESP_LOGW(TAG, "FET On Time aus NVS ungültig (%lu ms), verwende Default: %lu ms", value, g_servo_config.fet_on_time_ms);
        }
    } else {
        ESP_LOGI(TAG, "FET On Time nicht in NVS, verwende Default: %lu ms", g_servo_config.fet_on_time_ms);
    }
}

/**
 * @brief Servo-Konfiguration in NVS speichern
 */
esp_err_t save_servo_config_to_nvs(void)
{
    esp_err_t ret;

    ret = nvs_set_u32(g_nvs_handle, NVS_KEY_CLOSE_TIMEOUT_MS, g_servo_config.close_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Close Timeout speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(g_nvs_handle, NVS_KEY_SERVO_OPEN_US, g_servo_config.servo_open_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo Open speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(g_nvs_handle, NVS_KEY_SERVO_CLOSE_US, g_servo_config.servo_close_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo Close speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(g_nvs_handle, NVS_KEY_FET_ON_TIME_MS, g_servo_config.fet_on_time_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FET On Time speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Commit fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Servo-Konfiguration in NVS gespeichert");
    return ESP_OK;
}

// WiFi-Event-Handler für SNTP-Initialisierung nach Verbindung
static void wifi_ip_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi verbunden, initialisiere SNTP");
        initialize_sntp();
    }
}

// Status-Variablen (einzige Wahrheit für "Ventil offen" ist das servo-Modul)
static uint32_t activation_count = 0;

// Synchronisation (schützt activation_count)
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
    
    // WS2812B Status-LED (GPIO48) - Fehler ist nicht kritisch
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED-Init fehlgeschlagen (nicht kritisch): %s", esp_err_to_name(ret));
    }

    ret = stack_monitor_init();
    if (ret != ESP_OK) return ret;
    
    ret = heap_monitor_init();
    if (ret != ESP_OK) return ret;

    ret = watchdog_init();
    if (ret != ESP_OK) return ret;

    ret = wifi_init();
    if (ret != ESP_OK) return ret;

    // WiFi-Event-Handler für SNTP-Initialisierung registrieren
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_ip_event_handler, NULL);

    ret = web_server_init();
    if (ret != ESP_OK) return ret;

    ret = web_server_start();
    if (ret != ESP_OK) return ret;

    ret = ota_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Hardware initialisiert");
    return ESP_OK;
}

void deinit_hardware(void)
{
    // Mutex löschen
    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Hardware deinitialisiert");
}

// ============================================================================
// Deinitialisierung
// ============================================================================
// Cleanup-Infrastruktur (kein Shutdown-Pfad in always-on Firmware, daher bewusst ungenutzt)
__attribute__((unused)) static void deinit_modules(void)
{
    // Alle Module deinitialisieren (Mutex cleanup)
    wifi_module_deinit();
    stack_monitor_deinit();
    heap_monitor_deinit();
    servo_deinit();
    ota_deinit();
    error_log_deinit();
    deinit_hardware();

    ESP_LOGI(TAG, "Alle Module deinitialisiert");
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
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Servo-Konfiguration aus NVS laden
    load_servo_config_from_nvs();

    // Aktivierungszykler aus NVS laden
    ret = nvs_get_u32(g_nvs_handle, NVS_ACTIVATION_COUNT_KEY, &activation_count);
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
    esp_err_t ret = nvs_set_u32(g_nvs_handle, NVS_ACTIVATION_COUNT_KEY, activation_count);
    if (ret == ESP_OK) {
        nvs_commit(g_nvs_handle);
        ESP_LOGI(TAG, "Aktivierungszykler gespeichert: %lu", activation_count);
    } else {
        ESP_LOGE(TAG, "Fehler beim Speichern der Aktivierungszykler");
    }
}

/**
 * @brief Wasserhahn öffnen (Ventil-Zustand wird vom servo-Modul gehalten)
 */
static void open_water_valve(void)
{
    if (servo_is_valve_open()) {
        ESP_LOGW(TAG, "Wasserhahn bereits geöffnet");
        return;
    }
    
    servo_open_valve();
    led_set_state(LED_STATE_OPEN);  // blau: Wasserhahn offen
    
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    activation_count++;
    uint32_t count = activation_count;
    xSemaphoreGive(state_mutex);

    ESP_LOGI(TAG, "Wasserhahn geöffnet (Zyklus %lu)", count);
}

/**
 * @brief Wasserhahn schließen
 */
static void close_water_valve(void)
{
    if (!servo_is_valve_open()) {
        return;
    }
    
    servo_close_valve();
    led_set_state(LED_STATE_IDLE);  // grün: System bereit
    ESP_LOGI(TAG, "Wasserhahn geschlossen");
}

/**
 * @brief Steuerungs-Task - Wasserhahn-Logik (vereinfacht)
 *
 * Ablauf für vereinfachte PIR-Erkennung:
 *  - Geschlossen: bei HIGH-Signal sofort öffnen
 *  - Offen: bei HIGH-Signal Timeout zurücksetzen; nach close_timeout_ms ohne HIGH schließen
 *  - Kein Cooldown, keine komplexe Bewegungsphasen
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Steuerungs-Task gestartet (Core %d)", xPortGetCoreID());

    // Task beim Watchdog anmelden
    watchdog_subscribe();

    // Task-lokale Zustände (nur in diesem Task verwendet -> kein Mutex nötig)
    uint64_t last_motion_open = 0;  // Letzte HIGH-Signal während Ventil offen
    led_state_t last_led = LED_STATE_IDLE;  // Zuletzt gesetzte LED-Farbe (nur bei Änderung senden)

    while (1) {
        uint64_t now = esp_timer_get_time();
        bool motion = pir_get_gpio_level();
        bool valve_open = servo_is_valve_open();

        // Kalibrierung ausführen wenn ausgelöst (nicht im Interrupt-Kontext)
        if (calibration_pending) {
            calibration_pending = false;
            servo_calibrate();
        }

        // Nacht-Modus: Um Stoppzeit gepufferte Nachrichten senden
        time_t now_time;
        struct tm timeinfo;
        time(&now_time);
        localtime_r(&now_time, &timeinfo);
        
        int night_end_hour = telegram_get_night_end_hour();
        if (timeinfo.tm_hour == night_end_hour && timeinfo.tm_min == 0 && timeinfo.tm_mday != last_night_send_day) {
            telegram_send_night_buffer();
            last_night_send_day = timeinfo.tm_mday;
        }

        if (!valve_open) {
            // Wasserhahn geschlossen: bei HIGH sofort öffnen
            if (motion) {
                ESP_LOGI(TAG, "PIR HIGH -> Wasserhahn öffnen");
                open_water_valve();
                last_motion_open = now;
            }
        } else {
            // Wasserhahn offen: bei HIGH Timeout zurücksetzen
            if (motion) {
                last_motion_open = now;
                ESP_LOGD(TAG, "HIGH-Signal während offen -> Timeout zurückgesetzt");
            }
            // Prüfen ob Timeout erreicht
            uint64_t time_since_motion = (now - last_motion_open) / 1000ULL;
            if (time_since_motion >= g_servo_config.close_timeout_ms) {
                ESP_LOGI(TAG, "Timeout ohne HIGH-Signal (%llu ms) -> Wasserhahn schließen", (unsigned long long)time_since_motion);
                close_water_valve();
            }
        }

        // Status-LED aktualisieren (nur bei Änderung senden)
        led_state_t led_target;
        if (valve_open) {
            led_target = LED_STATE_OPEN;        // blau: Wasserhahn offen
        } else if (motion) {
            led_target = LED_STATE_MOTION;      // gelb: Bewegung erkannt
        } else {
            led_target = LED_STATE_IDLE;        // grün: bereit
        }
        if (led_target != last_led) {
            led_set_state(led_target);
            last_led = led_target;
        }

        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(DELAY_500MS_MS));
    }
}

/**
 * @brief Hauptanwendungs-Task - Taster und Status-Logging
 */
static void app_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Hauptanwendungs-Task gestartet (Core %d)", xPortGetCoreID());
    
    // Task beim Watchdog anmelden
    watchdog_subscribe();
    
    while (1) {
        // Manueller Taster prüfen
        if (gpio_get_level(GPIO_BUTTON) == 0) {
            vTaskDelay(pdMS_TO_TICKS(DELAY_50MS_MS));  // Entprellung
            if (gpio_get_level(GPIO_BUTTON) == 0) {
                ESP_LOGI(TAG, "Manueller Taster gedrückt");
                if (!servo_is_valve_open()) {
                    open_water_valve();
                } else {
                    close_water_valve();
                }
                vTaskDelay(pdMS_TO_TICKS(DELAY_1S_MS));  // Verhindere mehrfache Aktivierung
            }
        }
        
        // Status-Logging alle 30 Sekunden (64-Bit Tick-Count für Overflow-Sicherheit)
        static uint64_t last_status = 0;
        uint64_t current_tick = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_tick - last_status > DELAY_30S_MS) {
            bool valve_open = servo_is_valve_open();
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            uint32_t activations = activation_count;
            xSemaphoreGive(state_mutex);

            ESP_LOGI(TAG, "Status - Wasserhahn: %s, Zykler: %lu",
                     valve_open ? "OFFEN" : "ZU", activations);
            last_status = current_tick;
        }

        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(DELAY_500MS_MS));  // 500ms Zyklus
    }
}

/**
 * @brief Anwendungseinstiegspunkt
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Katzenbrunnen ESP32-S3 gestartet");
    ESP_LOGI(TAG, "Version: %s", APP_VERSION);
    
    // NVS zuerst initialisieren (WiFi/esp_wifi_init benötigt NVS)
    if (init_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Initialisierung fehlgeschlagen");
        return;
    }
    
    // Hardware initialisieren (inkl. WiFi/Web/OTA)
    if (init_hardware() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware-Initialisierung fehlgeschlagen");
        return;
    }

    // Telegram initialisieren
    if (telegram_init() != ESP_OK) {
        ESP_LOGW(TAG, "Telegram-Initialisierung fehlgeschlagen (optional)");
    }
    
    // System-Info ausgeben
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip-Revision: %d", chip_info.revision);
    ESP_LOGI(TAG, "Freier Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimal freier Heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "ESP32-S3 erkannt: %s",
             strcmp(CONFIG_IDF_TARGET, "esp32s3") == 0 ? "Ja" : "Nein");

    // SNTP wird automatisch nach WiFi-Verbindung initialisiert (wifi_ip_event_handler)

    // Module-Tasks starten
    if (pir_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "PIR-Task Start fehlgeschlagen");
    }

    if (stack_monitor_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Stack-Monitor-Task Start fehlgeschlagen");
    }
    
    if (heap_monitor_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Heap-Monitor-Task Start fehlgeschlagen");
    }
    
    if (watchdog_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog-Task Start fehlgeschlagen");
    }

    if (wifi_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Task Start fehlgeschlagen");
    }

    if (ota_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "OTA-Health-Check Task Start fehlgeschlagen");
    }

    // Steuerungs-Task erstellen (Core 0)
    BaseType_t ret = xTaskCreatePinnedToCore(
        control_task,
        "control_task",
        TASK_STACK_CONTROL,
        NULL,
        TASK_PRIO_CONTROL,
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
        TASK_STACK_APP,
        NULL,
        TASK_PRIO_APP,
        NULL,
        TASK_CORE_CONTROL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Application Tasks");
    } else {
        ESP_LOGI(TAG, "Katzenbrunnen erfolgreich initialisiert");
        ESP_LOGI(TAG, "Task-Architektur: PIR/Stack/WDT/WiFi/Control/App auf Core 0");
    }
}
