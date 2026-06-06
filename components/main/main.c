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
#include "led.h"

static const char *TAG = "katzenbrunnen";

// NVS handle für persistenten Speicher
nvs_handle_t g_nvs_handle;

// Servo Runtime-Konfiguration
servo_config_t g_servo_config = {
    .motion_timeout_ms = MOTION_TIMEOUT_MS,
    .servo_open_us = SERVO_OPEN_ANGLE_US,
    .servo_close_us = SERVO_CLOSE_ANGLE_US,
    .fet_on_time_ms = 5000
};

// SNTP Zeit-Synchronisation
static bool sntp_initialized = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Zeit synchronisiert: %s", ctime(&tv->tv_sec));

    // Servo-Kalibrierung nach Zeitsynchronisierung ausführen
    servo_calibrate();
}

static void initialize_sntp(void)
{
    if (sntp_initialized) {
        return;
    }
    ESP_LOGI(TAG, "Initialisiere SNTP");
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

    // motion_timeout_ms (10-300s = 10000-300000ms)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_MOTION_TIMEOUT_MS, &value);
    if (ret == ESP_OK) {
        if (value >= 10000 && value <= 300000) {
            g_servo_config.motion_timeout_ms = value;
            ESP_LOGI(TAG, "Motion Timeout aus NVS: %lu ms", value);
        } else {
            ESP_LOGW(TAG, "Motion Timeout aus NVS ungültig (%lu ms), verwende Default: %lu ms", value, g_servo_config.motion_timeout_ms);
        }
    } else {
        ESP_LOGI(TAG, "Motion Timeout nicht in NVS, verwende Default: %lu ms", g_servo_config.motion_timeout_ms);
    }

    // servo_open_us (100-1000µs)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_SERVO_OPEN_US, &value);
    if (ret == ESP_OK) {
        if (value >= 100 && value <= 1000) {
            g_servo_config.servo_open_us = value;
            ESP_LOGI(TAG, "Servo Open aus NVS: %lu us", value);
        } else {
            ESP_LOGW(TAG, "Servo Open aus NVS ungültig (%lu us), verwende Default: %lu us", value, g_servo_config.servo_open_us);
        }
    } else {
        ESP_LOGI(TAG, "Servo Open nicht in NVS, verwende Default: %lu us", g_servo_config.servo_open_us);
    }

    // servo_close_us (100-1000µs)
    ret = nvs_get_u32(g_nvs_handle, NVS_KEY_SERVO_CLOSE_US, &value);
    if (ret == ESP_OK) {
        if (value >= 100 && value <= 1000) {
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

    ret = nvs_set_u32(g_nvs_handle, NVS_KEY_MOTION_TIMEOUT_MS, g_servo_config.motion_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Motion Timeout speichern fehlgeschlagen: %s", esp_err_to_name(ret));
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

    // WiFi-Event-Handler für SNTP-Initialisierung registrieren
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_ip_event_handler, NULL);

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

    // GPIO Wake-Up für Deep Sleep aktivieren (ESP32-S3: ext1-Wakeup-API)
    esp_err_t sleep_ret = esp_sleep_enable_ext1_wakeup_io(
        BIT(POWER_DEEP_SLEEP_WAKEUP_GPIO),
        POWER_DEEP_SLEEP_WAKEUP_LEVEL ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW
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
 * @brief Aktivierungszykler in NVS speichern (nur vor Deep Sleep aufrufen)
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
    // NVS-Speicherung entfernt - wird nur vor Deep Sleep durchgeführt (vermeidet Watchdog-Trigger)
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
 * @brief Steuerungs-Task - Wasserhahn-Logik (alleinige Entscheidungsinstanz)
 *
 * Ablauf für pulsierendes PIR-Signal (2.5s HIGH → 5s LOW → 2.5s HIGH):
 *  - Geschlossen: bei Bewegungserkennung Timer starten; wenn über MIN_MOTION_DURATION_MS
 *    genügend HIGH-Signale kommen, öffnen (sofern kein Cooldown aktiv ist).
 *  - Offen: bei HIGH-Signal Timeout zurücksetzen; nach MOTION_TIMEOUT_MS ohne
 *    HIGH-Signal schließen und Cooldown starten.
 */
static void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Steuerungs-Task gestartet (Core %d)", xPortGetCoreID());

    // Task beim Watchdog anmelden
    watchdog_subscribe();

    // Task-lokale Zustände (nur in diesem Task verwendet -> kein Mutex nötig)
    uint64_t motion_begin = 0;      // Beginn der aktuellen Bewegungsphase
    uint64_t last_motion_open = 0;  // Letzte HIGH-Signal während Ventil offen
    uint64_t last_high_signal = 0;  // Letztes HIGH-Signal (für pulsierendes PIR)
    uint64_t cooldown_until = 0;    // Zeitpunkt bis Cooldown aktiv ist
    uint64_t last_any_motion = 0;   // Letzte beliebige Bewegung (für Deep Sleep)
    led_state_t last_led = LED_STATE_IDLE;  // Zuletzt gesetzte LED-Farbe (nur bei Änderung senden)

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
                last_high_signal = 0;
            } else if (motion) {
                // HIGH-Signal erkannt
                if (motion_begin == 0) {
                    motion_begin = now;  // Bewegungsphase beginnt
                    last_high_signal = now;
                } else {
                    last_high_signal = now;  // Timer für "Objekt weg" zurücksetzen
                    // Prüfen ob genügend Bewegung über Zeit erkannt wurde
                    if ((now - motion_begin) >= (MIN_MOTION_DURATION_MS * 1000ULL)) {
                        ESP_LOGI(TAG, "Bewegung >= %d ms -> Wasserhahn öffnen", MIN_MOTION_DURATION_MS);
                        open_water_valve();
                        last_motion_open = now;
                        motion_begin = 0;
                        last_high_signal = 0;
                    }
                }
            } else {
                // LOW-Signal (kein HIGH)
                if (motion_begin > 0) {
                    // Prüfen ob Objekt weg (kein HIGH für PIR_MOTION_TIMEOUT_MS)
                    if ((now - last_high_signal) >= (PIR_MOTION_TIMEOUT_MS * 1000ULL)) {
                        // Objekt weg -> Bewegungsphase abbrechen
                        uint64_t duration_ms = (now - motion_begin) / 1000ULL;
                        if (duration_ms >= 1000) {  // Nur Ereignisse >= 1 Sekunde protokollieren
                            pir_add_event(duration_ms);
                        }
                        motion_begin = 0;
                        last_high_signal = 0;
                    }
                }
            }
        } else {
            if (motion) {
                last_motion_open = now;
                ESP_LOGD(TAG, "HIGH-Signal während offen -> Timeout zurückgesetzt");
            }
            uint64_t time_since_motion = (now - last_motion_open) / 1000ULL;
            if (time_since_motion >= g_servo_config.motion_timeout_ms) {
                ESP_LOGI(TAG, "Timeout ohne Bewegung (%llu ms) -> Wasserhahn schließen", (unsigned long long)time_since_motion);
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

                    // Light Sleep mit GPIO Wake-Up konfigurieren (IDF 6.0: pro-GPIO Level + globale Aktivierung)
                    gpio_wakeup_enable(POWER_DEEP_SLEEP_WAKEUP_GPIO,
                        POWER_DEEP_SLEEP_WAKEUP_LEVEL ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
                    esp_sleep_enable_gpio_wakeup();

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

                    // Deep Sleep Wake-Up per PIR-GPIO konfigurieren
                    configure_deep_sleep_wakeup();

                    // Deep Sleep aktivieren
                    esp_deep_sleep_start();
                }
            }
        }
#endif

        // Status-LED aktualisieren (nur bei Änderung senden)
        led_state_t led_target;
        if (valve_open) {
            led_target = LED_STATE_OPEN;        // blau: Wasserhahn offen
        } else if (motion && now >= cooldown_until) {
            led_target = LED_STATE_MOTION;      // gelb: Bewegung erkannt
        } else {
            led_target = LED_STATE_IDLE;        // grün: bereit
        }
        if (led_target != last_led) {
            led_set_state(led_target);
            last_led = led_target;
        }

        // Batterie-Abschaltung bei kritischer Spannung
        if (battery_is_critical()) {
            ESP_LOGE(TAG, "Kritische Batteriespannung -> Wasserhahn schließen und Deep Sleep");
            led_set_state(LED_STATE_CRITICAL);  // rot: kritische Batterie
            if (valve_open) {
                close_water_valve();
            }

            // Watchdog deaktivieren vor Deep Sleep
            watchdog_stop();

            // Deep Sleep aktivieren (nur durch Reset aufweckbar)
            esp_deep_sleep_start();
        }

        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(DELAY_100MS_MS));
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
        
        // Status-Logging alle 30 Sekunden
        static uint32_t last_status = 0;
        uint32_t current_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_tick - last_status > DELAY_30S_MS) {
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
        vTaskDelay(pdMS_TO_TICKS(DELAY_100MS_MS));  // 100ms Zyklus
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

    // SNTP wird automatisch nach WiFi-Verbindung initialisiert (wifi_ip_event_handler)

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
