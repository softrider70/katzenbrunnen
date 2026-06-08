#include "wifi.h"
#include "config.h"
#include "error_log.h"
#include "dns_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"  // IP4_ADDR Makro für AP-IP-Konfiguration
// mDNS in ESP-IDF 6.1 erfordert externe Komponente - für jetzt deaktiviert
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
    bool ap_mode_forced;  // AP-Modus erzwungen (fehlende Credentials oder 3 Fehlversuche)
    char ssid[32];
    uint8_t retry_count;
    uint32_t last_error_code;
} wifi_state_t;

static wifi_state_t wifi_state = {
    .is_connected = false,
    .ap_active = false,
    .ap_mode_forced = false,
    .ssid = "",
    .retry_count = 0,
    .last_error_code = 0
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
            xSemaphoreTake(wifi_mutex, portMAX_DELAY);
            wifi_state.ap_mode_forced = true;
            xSemaphoreGive(wifi_mutex);
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
            // Nach 3 Fehlversuchen: AP-Modus erzwingen
            ESP_LOGW(TAG, "Maximale Retry-Anzahl erreicht (%d), AP-Modus wird erzwungen", WIFI_MAX_RETRY);
            xSemaphoreTake(wifi_mutex, portMAX_DELAY);
            wifi_state.ap_mode_forced = true;
            xSemaphoreGive(wifi_mutex);
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

    // Hostname setzen
    esp_netif_set_hostname(sta_netif, WIFI_HOSTNAME);
    esp_netif_set_hostname(ap_netif, WIFI_HOSTNAME);
    ESP_LOGI(TAG, "Hostname gesetzt: %s", WIFI_HOSTNAME);
    
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
            .channel = 1,
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

    // AP IP-Adresse konfigurieren
    esp_netif_ip_info_t ap_ip_info;
    IP4_ADDR(&ap_ip_info.ip, WIFI_AP_IP_OCTET1, WIFI_AP_IP_OCTET2, WIFI_AP_IP_OCTET3, WIFI_AP_IP_OCTET4);
    IP4_ADDR(&ap_ip_info.gw, WIFI_AP_IP_OCTET1, WIFI_AP_IP_OCTET2, WIFI_AP_IP_OCTET3, WIFI_AP_IP_OCTET4);
    IP4_ADDR(&ap_ip_info.netmask, WIFI_AP_NETMASK_OCTET1, WIFI_AP_NETMASK_OCTET2, WIFI_AP_NETMASK_OCTET3, WIFI_AP_NETMASK_OCTET4);
    ret = esp_netif_dhcps_stop(ap_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "AP DHCP-Stop fehlgeschlagen: %s", esp_err_to_name(ret));
    }
    ret = esp_netif_set_ip_info(ap_netif, &ap_ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP IP-Setzen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_netif_dhcps_start(ap_netif);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP DHCP-Start fehlgeschlagen: %s", esp_err_to_name(ret));
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

    // mDNS in ESP-IDF 6.1 erfordert externe Komponente - für jetzt deaktiviert
    // Hostname ist bereits gesetzt für DHCP
    ESP_LOGI(TAG, "Hostname gesetzt: %s (mDNS deaktiviert - externe Komponente erforderlich)", WIFI_HOSTNAME);

    return ESP_OK;
}

esp_err_t wifi_start_task(void)
{
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Start fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // WiFi Sendeleistung konfigurieren
    // ESP32-S3: 0-84 entspricht ~0-20dBm (Faktor ~4.2)
    int8_t tx_power_dbm = WIFI_TX_POWER;
    int8_t tx_power_hw = (int8_t)(tx_power_dbm * 4.2f);
    if (tx_power_hw < 0) tx_power_hw = 0;
    if (tx_power_hw > WIFI_TX_POWER_HW_MAX) tx_power_hw = WIFI_TX_POWER_HW_MAX;

    ret = esp_wifi_set_max_tx_power(tx_power_hw);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi Sendeleistung auf %ddBm gesetzt (HW: %d)", tx_power_dbm, tx_power_hw);
    } else {
        ESP_LOGW(TAG, "WiFi Sendeleistung konnte nicht gesetzt werden: %s", esp_err_to_name(ret));
    }

    // DNS-Server starten wenn AP-Modus erzwungen ist
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    bool ap_forced = wifi_state.ap_mode_forced;
    xSemaphoreGive(wifi_mutex);

    if (ap_forced) {
        ret = dns_server_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DNS-Server Start fehlgeschlagen: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "DNS-Server gestartet für Captive Portal");
        }
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
    vTaskDelay(pdMS_TO_TICKS(DELAY_1S_MS));
    esp_wifi_connect();
    
    return ESP_OK;
}

bool wifi_is_ap_mode_forced(void)
{
    bool forced;
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    forced = wifi_state.ap_mode_forced;
    xSemaphoreGive(wifi_mutex);
    return forced;
}

esp_err_t wifi_reset_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Öffnen fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // WiFi-Credentials löschen
    ret = nvs_erase_key(nvs_handle, NVS_KEY_WIFI_SSID);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "SSID-Löschen fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_erase_key(nvs_handle, NVS_KEY_WIFI_PASS);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Passwort-Löschen fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Commit fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // WiFi-State zurücksetzen
    xSemaphoreTake(wifi_mutex, portMAX_DELAY);
    wifi_state.ssid[0] = '\0';
    wifi_state.retry_count = 0;
    wifi_state.ap_mode_forced = true;
    has_sta_credentials = false;
    xSemaphoreGive(wifi_mutex);

    // WiFi neu starten um in den AP-Modus zu wechseln
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(DELAY_500MS_MS));
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi-Credentials gelöscht, AP-Modus wird erzwungen");
    return ESP_OK;
}

void wifi_module_deinit(void)
{
    // Mutex löschen
    if (wifi_mutex != NULL) {
        vSemaphoreDelete(wifi_mutex);
        wifi_mutex = NULL;
    }
    
    // Event-Group löschen
    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }
    
    ESP_LOGI(TAG, "WiFi-Modul deinitialisiert");
}
