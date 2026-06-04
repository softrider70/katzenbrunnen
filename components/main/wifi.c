#include "wifi.h"
#include "config.h"
#include "error_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

// WiFi-Event-Bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// WiFi-State
typedef struct {
    bool is_connected;
    bool ap_active;
    char ssid[32];
    uint8_t retry_count;
    uint32_t last_error_code;
    bool sleep_active;
} wifi_state_t;

static wifi_state_t wifi_state = {
    .is_connected = false,
    .ap_active = false,
    .ssid = "",
    .retry_count = 0,
    .last_error_code = 0,
    .sleep_active = false
};

static SemaphoreHandle_t wifi_mutex = NULL;
static EventGroupHandle_t wifi_event_group = NULL;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static bool has_sta_credentials = false;  // STA-Zugangsdaten vorhanden?

// WiFi-Event-Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Station gestartet");
        // Nur verbinden, wenn Zugangsdaten vorhanden sind
        if (has_sta_credentials) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Keine STA-Credentials - AP-Setup-Modus aktiv");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi getrennt: Grund %d", event->reason);
        
        xSemaphoreTake(wifi_mutex, portMAX_DELAY);
        wifi_state.is_connected = false;
        wifi_state.retry_count++;
        wifi_state.last_error_code = event->reason;
        uint8_t current_retry = wifi_state.retry_count;
        xSemaphoreGive(wifi_mutex);
        
        if (current_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP erhalten: " IPSTR, IP2STR(&event->ip_info.ip));
        
        xSemaphoreTake(wifi_mutex, portMAX_DELAY);
        wifi_state.is_connected = true;
        wifi_state.retry_count = 0;
        xSemaphoreGive(wifi_mutex);
        
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// WiFi-Task
static void wifi_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi-Task gestartet (Core %d)", xPortGetCoreID());
    
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi erfolgreich verbunden");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi-Verbindung fehlgeschlagen nach %d Versuchen", WIFI_MAX_RETRY);
        error_log_add(ERR_WIFI_FAILURE, "wifi_task", 2);
    }
    
    vTaskDelete(NULL);
}

esp_err_t wifi_init(void)
{
    esp_err_t ret;
    
    // Mutex erstellen
    wifi_mutex = xSemaphoreCreateMutex();
    if (wifi_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // Event-Group erstellen
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Event-Group-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // TCP/IP-Stack initialisieren
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Netif-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Event-Loop initialisieren
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event-Loop-Erstellung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Netif für STA und AP erstellen (APSTA für Setup-Fallback)
    sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "STA-Netif-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "AP-Netif-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    // WiFi-Initialisierung
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Initialisierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Event-Handler registrieren
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Event-Handler-Registrierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP-Event-Handler-Registrierung fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Immer APSTA: STA für Normalbetrieb, AP als Setup-Fallback
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Mode-Setzen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // AP-Konfiguration (Setup-Netzwerk)
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP-Config-Setzen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // STA-Konfiguration aus NVS laden (falls vorhanden)
    nvs_handle_t nvs_h;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h);
    if (ret == ESP_OK) {
        char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
        char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(password);
        
        if (nvs_get_str(nvs_h, NVS_KEY_WIFI_SSID, ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs_h, NVS_KEY_WIFI_PASS, password, &pass_len) == ESP_OK &&
            strlen(ssid) > 0) {
            
            wifi_config_t sta_config = {
                .sta = {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                },
            };
            
            strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
            strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
            
            xSemaphoreTake(wifi_mutex, portMAX_DELAY);
            strncpy(wifi_state.ssid, ssid, sizeof(wifi_state.ssid) - 1);
            wifi_state.ssid[sizeof(wifi_state.ssid) - 1] = '\0';
            xSemaphoreGive(wifi_mutex);
            
            ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "STA-Config-Setzen fehlgeschlagen: %s", esp_err_to_name(ret));
                nvs_close(nvs_h);
                return ret;
            }
            
            has_sta_credentials = true;
            ESP_LOGI(TAG, "WiFi-Credentials aus NVS geladen: %s", ssid);
        } else {
            ESP_LOGW(TAG, "Keine WiFi-Credentials in NVS - AP-Setup unter SSID '%s'", WIFI_AP_SSID);
        }
        
        nvs_close(nvs_h);
    } else {
        ESP_LOGW(TAG, "NVS nicht lesbar - AP-Setup unter SSID '%s'", WIFI_AP_SSID);
    }
    
    ESP_LOGI(TAG, "WiFi-Modul initialisiert (Modus: APSTA)");
    return ESP_OK;
}

esp_err_t wifi_start_task(void)
{
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Start fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        wifi_task,
        "wifi_task",
        TASK_STACK_WIFI,
        NULL,
        TASK_PRIO_WIFI,
        NULL,
        TASK_CORE_NETWORK
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des WiFi-Tasks");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    bool connected;
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    connected = wifi_state.is_connected;
    xSemaphoreGive(wifi_mutex);
    return connected;
}

void wifi_get_ssid(char *ssid)
{
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    strncpy(ssid, wifi_state.ssid, 31);
    ssid[31] = '\0';
    xSemaphoreGive(wifi_mutex);
}

int8_t wifi_get_rssi(void)
{
    if (!wifi_is_connected()) {
        return 0;
    }
    
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        return 0;
    }
    
    return ap_info.rssi;
}

void wifi_get_ip(char *ip_str)
{
    if (!wifi_is_connected()) {
        strcpy(ip_str, "Not connected");
        return;
    }
    
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(sta_netif, &ip_info);
    if (ret != ESP_OK) {
        strcpy(ip_str, "Error");
        return;
    }
    
    snprintf(ip_str, 16, IPSTR, IP2STR(&ip_info.ip));
}

esp_err_t wifi_set_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Öffnen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSID-Speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASS, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Passwort-Speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Commit fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Neue STA-Konfiguration sofort anwenden
    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    strncpy(wifi_state.ssid, ssid, sizeof(wifi_state.ssid) - 1);
    wifi_state.ssid[sizeof(wifi_state.ssid) - 1] = '\0';
    wifi_state.retry_count = 0;
    xSemaphoreGive(wifi_mutex);
    
    // Verbindungsversuch mit neuen Daten starten
    has_sta_credentials = true;
    esp_wifi_disconnect();
    esp_wifi_connect();
    
    ESP_LOGI(TAG, "WiFi-Credentials gespeichert, verbinde mit: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_reconnect(void)
{
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    wifi_state.retry_count = 0;
    xSemaphoreGive(wifi_mutex);
    
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_connect();
    
    return ESP_OK;
}

void wifi_set_sleep(bool enable)
{
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    wifi_state.sleep_active = enable;
    xSemaphoreGive(wifi_mutex);
    
    if (enable) {
        esp_wifi_stop();
        ESP_LOGI(TAG, "WiFi Sleep aktiviert");
    } else {
        esp_wifi_start();
        ESP_LOGI(TAG, "WiFi Sleep deaktiviert");
    }
}

bool wifi_is_sleep_active(void)
{
    bool active;
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    active = wifi_state.sleep_active;
    xSemaphoreGive(wifi_mutex);
    return active;
}
