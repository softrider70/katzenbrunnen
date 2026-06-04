#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "config.h"
#include "pir.h"
#include "servo.h"
#include "battery.h"
#include "error_log.h"
#include "stack_monitor.h"
#include "heap_monitor.h"
#include "watchdog.h"
#include "wifi.h"
#include "web_server.h"
#include "ota.h"

static const char *TAG = "katzenbrunnen";

// NVS handle für persistenten Speicher
nvs_handle_t g_nvs_handle;

// SNTP Zeit-Synchronisation
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Zeit synchronisiert: %s", ctime(&tv->tv_sec));
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initialisiere SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
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
    
    ret = battery_init();
    if (ret != ESP_OK) return ret;
    
    ret = stack_monitor_init();
    if (ret != ESP_OK) return ret;
    
    ret = heap_monitor_init();
    if (ret != ESP_OK) return ret;
    
    ret = watchdog_init();
    if (ret != ESP_OK) return ret;
    
    ret = wifi_init();
    if (ret != ESP_OK) return ret;
    
    ret = web_server_init();
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
// NVS Initialisierung
// ============================================================================
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS Flash leer oder Version geändert, formatiere...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS Flash-Erase fehlgeschlagen: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash-Init fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS initialisiert");
    return ESP_OK;
}

// ============================================================================
// SNTP Initialisierung
// ============================================================================
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initialisiere SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

// ============================================================================
// Hardware Initialisierung
// ============================================================================
static esp_err_t init_hardware(void)
{
    esp_err_t ret;
    
    // GPIO für Servo konfigurieren
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SERVO_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;
    
    // GPIO für PIR konfigurieren
    io_conf.pin_bit_mask = (1ULL << PIR_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
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
    
    ret = heap_monitor_init();
    if (ret != ESP_OK) return ret;
    
    ret = watchdog_init();
    if (ret != ESP_OK) return ret;
    
    ret = wifi_init();
    if (ret != ESP_OK) return ret;
    
    ret = web_server_init();
    if (ret != ESP_OK) return ret;
    
    ret = ota_init();
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "Hardware initialisiert");
    return ESP_OK;
}

// ============================================================================
// Deinitialisierung
// ============================================================================
static void deinit_modules(void)
{
    // Alle Module deinitialisieren (Mutex cleanup)
    wifi_deinit();
    stack_monitor_deinit();
    heap_monitor_deinit();
    servo_deinit();
    ota_deinit();
    error_log_deinit();
    battery_deinit();
    deinit_hardware();
    
    ESP_LOGI(TAG, "Alle Module deinitialisiert");
}

// ============================================================================
// Deep Sleep Wake-Up konfigurieren
// ============================================================================
static esp_err_t configure_deep_sleep_wakeup(void)
{
#if POWER_DEEP_SLEEP_ENABLE
    // GPIO4 als RTC GPIO konfigurieren für Deep Sleep Wake-Up
    rtc_gpio_init(POWER_DEEP_SLEEP_WAKEUP_GPIO);
    rtc_gpio_set_direction(POWER_DEEP_SLEEP_WAKEUP_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en(POWER_DEEP_SLEEP_WAKEUP_GPIO);
    rtc_gpio_pullup_dis(POWER_DEEP_SLEEP_WAKEUP_GPIO);

    // GPIO Wake-Up für Deep Sleep aktivieren
    esp_err_t sleep_ret = esp_sleep_enable_gpio_wakeup(
        BIT(POWER_DEEP_SLEEP_WAKEUP_GPIO),
        POWER_DEEP_SLEEP_WAKEUP_LEVEL ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW
    );

    if (sleep_ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO Wake-Up Konfiguration fehlgeschlagen: %s", esp_err_to_name(sleep_ret));
        return sleep_ret;
    }

    ESP_LOGI(TAG, "Deep Sleep Wake-Up konfiguriert: GPIO%d (Level=%s)",
             POWER_DEEP_SLEEP_WAKEUP_GPIO,
             POWER_DEEP_SLEEP_WAKEUP_LEVEL ? "HIGH" : "LOW");
#endif
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
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
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
    
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    activation_count++;
    uint32_t count = activation_count;
    xSemaphoreGive(state_mutex);
    
    ESP_LOGI(TAG, "Wasserhahn geöffnet (Zyklus %lu)", count);
    save_activation_count();
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
    ESP_LOGI(TAG, "Wasserhahn geschlossen");
}

/**
 * @brief Steuerungs-Task - Wasserhahn-Logik (alleinige Entscheidungsinstanz)
 *
 * Ablauf:
 *  - Geschlossen: bei durchgehender Bewegung >= MIN_MOTION_DURATION_MS öffnen,
 *    sofern kein Cooldown aktiv ist.
 *  - Offen: bei Bewegung Timeout zurücksetzen; nach MOTION_TIMEOUT_MS ohne
 *    Bewegung schließen und Cooldown starten.
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Steuerungs-Task gestartet (Core %d)", xPortGetCoreID());

    // Task beim Watchdog anmelden
    watchdog_subscribe();

    // Task-lokale Zustände (nur in diesem Task verwendet -> kein Mutex nötig)
    uint64_t motion_begin = 0;      // Beginn der aktuellen Bewegungsphase
    uint64_t last_motion_open = 0;  // Letzte Bewegung während Ventil offen
    uint64_t cooldown_until = 0;    // Zeitpunkt bis Cooldown aktiv ist
    uint64_t last_any_motion = 0;   // Letzte beliebige Bewegung (für Deep Sleep)

    while (1) {
        uint64_t now = esp_timer_get_time();
        bool motion = pir_motion_detected();
        bool valve_open = servo_is_valve_open();

        // Letzte Bewegung für Deep Sleep aktualisieren
        if (motion) {
            last_any_motion = now;
        }

        if (!valve_open) {
            if (now < cooldown_until) {
                // Cooldown läuft -> nicht öffnen
                motion_begin = 0;
            } else if (motion) {
                if (motion_begin == 0) {
                    motion_begin = now;  // Bewegungsphase beginnt
                } else if ((now - motion_begin) >= (MIN_MOTION_DURATION_MS * 1000ULL)) {
                    ESP_LOGI(TAG, "Bewegung >= %d ms -> Wasserhahn öffnen", MIN_MOTION_DURATION_MS);
                    open_water_valve();
                    last_motion_open = now;
                    motion_begin = 0;
                }
            } else {
                // Bewegung unterbrochen -> Ereignis protokollieren
                if (motion_begin > 0) {
                    uint64_t duration_ms = (now - motion_begin) / 1000ULL;
                    if (duration_ms >= 1000) {  // Nur Ereignisse >= 1 Sekunde protokollieren
                        pir_add_event(duration_ms);
                    }
                }
                motion_begin = 0;  // zurücksetzen
            }
        } else {
            if (motion) {
                last_motion_open = now;
            }
            if ((now - last_motion_open) >= (MOTION_TIMEOUT_MS * 1000ULL)) {
                ESP_LOGI(TAG, "Timeout ohne Bewegung -> Wasserhahn schließen");
                close_water_valve();
                cooldown_until = now + (PIR_COOLDOWN_MS * 1000ULL);
            }
        }

        // Deep Sleep Prüfung (nur wenn Ventil geschlossen und Cooldown abgelaufen)
#if POWER_DEEP_SLEEP_ENABLE
        if (!valve_open && now >= cooldown_until) {
            uint64_t inactivity_ms = (now - last_any_motion) / 1000ULL;
            if (inactivity_ms >= POWER_DEEP_SLEEP_TIMEOUT_MS) {
                // Prüfen ob nachts (Deep Sleep) oder tagsüber (Light Sleep)
                time_t now_sec = time(NULL);

                // Wenn Zeit nicht synchronisiert (Epoch < 1000000), immer Light Sleep verwenden
                // damit Katzen Wasser bekommen auch ohne korrekte Zeit
                bool use_light_sleep = false;
                if (now_sec < 1000000) {
                    ESP_LOGW(TAG, "Zeit nicht synchronisiert -> Light Sleep (Katzen müssen Wasser bekommen)");
                    use_light_sleep = true;
                } else {
                    struct tm *tm_info = localtime(&now_sec);
                    int current_hour = tm_info->tm_hour;

                    bool is_night = false;
                    if (WIFI_SLEEP_START_HOUR > WIFI_SLEEP_END_HOUR) {
                        // Sleep-Fenster über Mitternacht (z.B. 22:00 - 06:00)
                        is_night = (current_hour >= WIFI_SLEEP_START_HOUR || current_hour < WIFI_SLEEP_END_HOUR);
                    } else {
                        // Sleep-Fenster innerhalb eines Tages (z.B. 02:00 - 06:00)
                        is_night = (current_hour >= WIFI_SLEEP_START_HOUR && current_hour < WIFI_SLEEP_END_HOUR);
                    }

                    use_light_sleep = !is_night;
                }

                if (use_light_sleep) {
                    ESP_LOGI(TAG, "Inaktivität >= %d ms -> Light Sleep aktivieren", POWER_DEEP_SLEEP_TIMEOUT_MS);
                    ESP_LOGI(TAG, "PIR-Sensor (GPIO%d) wird Wake-Up auslösen", POWER_DEEP_SLEEP_WAKEUP_GPIO);

                    // Watchdog deaktivieren vor Light Sleep
                    watchdog_stop();

                    // Light Sleep mit GPIO Wake-Up konfigurieren
                    esp_sleep_enable_gpio_wakeup(
                        BIT(POWER_DEEP_SLEEP_WAKEUP_GPIO),
                        POWER_DEEP_SLEEP_WAKEUP_LEVEL ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW
                    );

                    // Light Sleep aktivieren
                    esp_light_sleep_start();

                    // Watchdog nach Light Sleep neu starten
                    watchdog_start();
                    watchdog_subscribe();
                } else {
                    ESP_LOGI(TAG, "Inaktivität >= %d ms (Nacht) -> Deep Sleep aktivieren", POWER_DEEP_SLEEP_TIMEOUT_MS);
                    ESP_LOGI(TAG, "PIR-Sensor (GPIO%d) wird Wake-Up auslösen", POWER_DEEP_SLEEP_WAKEUP_GPIO);

                    // Watchdog deaktivieren vor Deep Sleep
                    watchdog_stop();

                    // Deep Sleep aktivieren
                    esp_deep_sleep_start();
                }
            }
        }
#endif

        // Batterie-Abschaltung bei kritischer Spannung
        if (battery_is_critical()) {
            ESP_LOGE(TAG, "Kritische Batteriespannung -> Wasserhahn schließen und Deep Sleep");
            if (valve_open) {
                close_water_valve();
            }

            // Watchdog deaktivieren vor Deep Sleep
            watchdog_stop();

            // Deep Sleep aktivieren (nur durch Reset aufweckbar)
            esp_deep_sleep_start();
        }

        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(100));
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
            bool valve_open = servo_is_valve_open();
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            uint32_t activations = activation_count;
            xSemaphoreGive(state_mutex);
            
            float batt_voltage = battery_get_voltage();
            uint8_t batt_percent = battery_get_percent();
            
            ESP_LOGI(TAG, "Status - Wasserhahn: %s, Zykler: %lu, Batterie: %.2fV (%d%%)", 
                     valve_open ? "OFFEN" : "ZU", activations, batt_voltage, batt_percent);
            last_status = current_tick;
        }
        
        watchdog_feed();
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
    
    // System-Info ausgeben
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip-Revision: %d", chip_info.revision);
    ESP_LOGI(TAG, "Freier Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimal freier Heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "ESP32-S3 erkannt: %s",
             strcmp(CONFIG_IDF_TARGET, "esp32s3") == 0 ? "Ja" : "Nein");

    // SNTP für Zeit-Synchronisation initialisieren
    initialize_sntp();
    
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
    
    if (heap_monitor_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Heap-Monitor-Task Start fehlgeschlagen");
    }
    
    if (watchdog_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog-Task Start fehlgeschlagen");
    }
    
    if (wifi_start_task() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Task Start fehlgeschlagen");
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
        ESP_LOGI(TAG, "Task-Architektur: PIR/Battery/Stack/WDT/WiFi/Control/App auf Core 0");
    }
}
