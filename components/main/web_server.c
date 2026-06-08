#include "web_server.h"
#include "config.h"
#include "error_log.h"
#include "wifi.h"
#include "servo.h"
#include "pir.h"
#include "ota.h"
#include "stack_monitor.h"
#include "heap_monitor.h"
#include "telegram.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>

static const char *TAG = "web_server";
static httpd_handle_t server = NULL;

// Statische Puffer für HTTP-Responses (vermeidet Heap-Fragmentierung)
static error_log_entry_t error_entries_buffer[ERROR_LOG_MAX_ENTRIES];
static char error_json_buffer[8192];
static stack_info_t stack_info_buffer[16];
static char stack_json_buffer[2048];

// JSON Escape Helper
static void json_escape_string(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        switch (src[i]) {
            case '"':  if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\b': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = 'b'; } break;
            case '\f': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = 'f'; } break;
            case '\n': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j + 1 < dst_size) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

// JSON Response Helper
static void send_json_response(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
}

// ============================================================================
// API Handler: GET /api/status - System Status
// ============================================================================
static esp_err_t status_handler(httpd_req_t *req)
{
    char json_response[512];
    char ip_str[16] = "Not connected";
    char ssid[32] = "Not connected";

    wifi_get_ip(ip_str);
    wifi_get_ssid(ssid);

    bool valve_open = servo_is_valve_open();
    bool motion_detected = pir_motion_detected();
    int8_t rssi = wifi_get_rssi();

    heap_info_t heap_info;
    heap_monitor_get_info(&heap_info);

    snprintf(json_response, sizeof(json_response),
        "{"
        "\"status\":\"%s\","
        "\"valve_open\":%s,"
        "\"motion_detected\":%s,"
        "\"wifi_connected\":%s,"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_ip\":\"%s\","
        "\"wifi_rssi\":%d,"
        "\"heap_total\":%lu,"
        "\"heap_free\":%lu,"
        "\"heap_min_free\":%lu,"
        "\"heap_percent\":%u,"
        "\"heap_warning\":%s,"
        "\"heap_critical\":%s,"
        "\"uptime_ms\":%llu,"
        "\"version\":\"%s\","
        "\"build_number\":%d"
        "}",
        valve_open ? "OPEN" : "CLOSED",
        valve_open ? "true" : "false",
        motion_detected ? "true" : "false",
        wifi_is_connected() ? "true" : "false",
        ssid,
        ip_str,
        rssi,
        (unsigned long)heap_info.total_heap,
        (unsigned long)heap_info.free_heap,
        (unsigned long)heap_info.min_free_heap,
        heap_info.free_percent,
        heap_info.warning ? "true" : "false",
        heap_info.critical ? "true" : "false",
        (unsigned long long)(esp_timer_get_time() / 1000),
        VERSION_STRING,
        BUILD_NUMBER
    );
    
    send_json_response(req, json_response);
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/errors - Error Log (FIFO)
// ============================================================================
static esp_err_t errors_handler(httpd_req_t *req)
{
    // Statische Puffer verwenden (vermeidet Heap-Fragmentierung)
    uint16_t count = error_log_get_all(error_entries_buffer, ERROR_LOG_MAX_ENTRIES);
    
    int offset = snprintf(error_json_buffer, sizeof(error_json_buffer), "{\"errors\":[");
    
    for (uint16_t i = 0; i < count; i++) {
        char escaped_task[32] = {0};
        json_escape_string(error_entries_buffer[i].task_name, escaped_task, sizeof(escaped_task));
        
        // Pufferüberlauf vermeiden: vor jedem Eintrag Restplatz prüfen
        if ((size_t)offset >= sizeof(error_json_buffer) - 160) {
            break;
        }
        
        offset += snprintf(error_json_buffer + offset, sizeof(error_json_buffer) - offset,
            "%s{\"code\":\"%s\",\"id\":%d,\"timestamp_ms\":%llu,\"task\":\"%s\",\"severity\":%d}",
            (i > 0) ? "," : "",
            error_entries_buffer[i].code,
            error_entries_buffer[i].error_id,
            (unsigned long long)error_entries_buffer[i].timestamp_ms,
            escaped_task,
            error_entries_buffer[i].severity
        );
    }
    
    snprintf(error_json_buffer + offset, sizeof(error_json_buffer) - offset, "],\"count\":%d}", count);
    
    send_json_response(req, error_json_buffer);
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/error_codes - Error Code Translation Table
// ============================================================================
static esp_err_t error_codes_handler(httpd_req_t *req)
{
    char json_response[512];
    
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"error_codes\":{"
        "\"0x01\":\"%s\","
        "\"0x02\":\"%s\","
        "\"0x03\":\"%s\","
        "\"0x04\":\"%s\","
        "\"0x05\":\"%s\","
        "\"0x06\":\"%s\","
        "\"0x07\":\"%s\","
        "\"0x08\":\"%s\","
        "\"0x09\":\"%s\","
        "\"0x0A\":\"%s\""
        "}"
        "}",
        error_log_get_text(0x01),
        error_log_get_text(0x02),
        error_log_get_text(0x03),
        error_log_get_text(0x04),
        error_log_get_text(0x05),
        error_log_get_text(0x06),
        error_log_get_text(0x07),
        error_log_get_text(0x08),
        error_log_get_text(0x09),
        error_log_get_text(0x0A)
    );
    
    send_json_response(req, json_response);
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/errors/clear - Clear Error Log
// ============================================================================
static esp_err_t errors_clear_handler(httpd_req_t *req)
{
    error_log_clear();
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Error log cleared\"}");
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/ota/status - OTA Status
// ============================================================================
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    bool in_progress = false;
    bool last_result_ok = false;
    char phase[32] = {0};
    char message[64] = {0};

    ota_get_status(&in_progress, &last_result_ok, phase, message);

    char json_response[256];
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"status\":\"%s\","
        "\"ota\":{"
        "\"in_progress\":%s,"
        "\"last_result_ok\":%s,"
        "\"phase\":\"%s\","
        "\"message\":\"%s\""
        "}"
        "}",
        in_progress ? "BUSY" : "OK",
        in_progress ? "true" : "false",
        last_result_ok ? "true" : "false",
        phase,
        message
    );

    send_json_response(req, json_response);
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/ota/start - Start OTA Update
// ============================================================================
static esp_err_t ota_start_handler(httpd_req_t *req)
{
    char body[320] = {0};
    char url[192] = {0};

    int total_len = req->content_len;
    if (total_len >= sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Content too large\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Request timeout\"}");
        } else {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        }
        return ESP_OK;
    }

    // Simple JSON parsing for URL
    const char *url_key = "\"url\":\"";
    char *url_start = strstr(body, url_key);
    if (url_start == NULL) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"URL not found\"}");
        return ESP_OK;
    }

    url_start += strlen(url_key);
    char *url_end = strchr(url_start, '"');
    if (url_end == NULL) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    size_t url_len = url_end - url_start;
    if (url_len >= sizeof(url)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"URL too long\"}");
        return ESP_OK;
    }

    strncpy(url, url_start, url_len);
    url[url_len] = '\0';

    esp_err_t ret = ota_start_update(url);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"OTA bereits aktiv\"}");
        } else {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"OTA start failed\"}");
        }
        return ESP_OK;
    }

    send_json_response(req, "{\"status\":\"OK\",\"message\":\"OTA update started\"}");
    return ESP_OK;
}

// ============================================================================
// Hilfsfunktion: JSON-String-Feld einfach parsen ("key":"value")
// ============================================================================
static bool parse_json_field(const char *body, const char *key,
                             char *out, size_t out_size)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }
    size_t len = end - start;
    if (len >= out_size) {
        return false;
    }
    strncpy(out, start, len);
    out[len] = '\0';
    return true;
}

// ============================================================================
// Hilfsfunktion: JSON-Number-Feld parsen ("key":123)
// ============================================================================
static bool parse_json_number(const char *body, const char *key, uint32_t *out)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);
    // Überspringe Whitespace
    while (*start == ' ' || *start == '\t') start++;
    // Parse Zahl
    char *endptr;
    *out = (uint32_t)strtoul(start, &endptr, 10);
    // Prüfe ob Parsing erfolgreich war
    if (endptr == start) {
        return false;
    }
    return true;
}

// ============================================================================
// API Handler: POST /api/wifi - WiFi-Credentials setzen
// ============================================================================
static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char body[256] = {0};
    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};
    
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }
    
    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }
    
    if (!parse_json_field(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"SSID fehlt\"}");
        return ESP_OK;
    }
    // Passwort darf leer sein (offenes Netz)
    parse_json_field(body, "password", password, sizeof(password));
    
    esp_err_t ret = wifi_set_credentials(ssid, password);
    if (ret != ESP_OK) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Speichern fehlgeschlagen\"}");
        return ESP_OK;
    }
    
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"WiFi gespeichert, verbinde...\"}");
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/servo_config - Servo-Konfiguration laden
// ============================================================================
static esp_err_t servo_config_get_handler(httpd_req_t *req)
{
    char json_response[256];
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"status\":\"OK\","
        "\"close_timeout_s\":%lu,"
        "\"servo_open_us\":%lu,"
        "\"servo_close_us\":%lu,"
        "\"fet_on_time_s\":%lu"
        "}",
        g_servo_config.close_timeout_ms / 1000,
        g_servo_config.servo_open_us,
        g_servo_config.servo_close_us,
        g_servo_config.fet_on_time_ms / 1000
    );
    send_json_response(req, json_response);
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/servo_config - Servo-Konfiguration speichern
// ============================================================================
static esp_err_t servo_config_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    uint32_t close_timeout_s, servo_open_us, servo_close_us, fet_on_time_s;

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }

    // JSON-Felder parsen (mit parse_json_number für numerische Werte)
    if (!parse_json_number(body, "close_timeout_s", &close_timeout_s) ||
        close_timeout_s < 1 || close_timeout_s > 30) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Ungueltiger close_timeout_s (1-30s)\"}");
        return ESP_OK;
    }

    if (!parse_json_number(body, "servo_open_us", &servo_open_us) ||
        servo_open_us < 50 || servo_open_us > 20000) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Ungueltiger servo_open_us (50-20000us)\"}");
        return ESP_OK;
    }

    if (!parse_json_number(body, "servo_close_us", &servo_close_us) ||
        servo_close_us < 50 || servo_close_us > 20000) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Ungueltiger servo_close_us (50-20000us)\"}");
        return ESP_OK;
    }

    if (!parse_json_number(body, "fet_on_time_s", &fet_on_time_s) ||
        fet_on_time_s < 1 || fet_on_time_s > 10) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Ungueltiger fet_on_time_s (1-10s)\"}");
        return ESP_OK;
    }

    // Werte in globale Konfiguration übernehmen
    g_servo_config.close_timeout_ms = close_timeout_s * 1000;
    g_servo_config.servo_open_us = servo_open_us;
    g_servo_config.servo_close_us = servo_close_us;
    g_servo_config.fet_on_time_ms = fet_on_time_s * 1000;

    // In NVS speichern (direkt in web_server.c, da g_nvs_handle nicht verfügbar)
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen: %s", esp_err_to_name(ret));
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS open failed\"}");
        return ESP_OK;
    }

    ret = nvs_set_u32(nvs_handle, NVS_KEY_CLOSE_TIMEOUT_MS, g_servo_config.close_timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Close Timeout speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS set close_timeout failed\"}");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Close Timeout erfolgreich in NVS gesetzt: %lu ms", g_servo_config.close_timeout_ms);

    ret = nvs_set_u32(nvs_handle, NVS_KEY_SERVO_OPEN_US, g_servo_config.servo_open_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo Open speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS set servo_open failed\"}");
        return ESP_OK;
    }

    ret = nvs_set_u32(nvs_handle, NVS_KEY_SERVO_CLOSE_US, g_servo_config.servo_close_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo Close speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS set servo_close failed\"}");
        return ESP_OK;
    }

    ret = nvs_set_u32(nvs_handle, NVS_KEY_FET_ON_TIME_MS, g_servo_config.fet_on_time_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FET On Time speichern fehlgeschlagen: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS set fet_on_time failed\"}");
        return ESP_OK;
    }

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS Commit fehlgeschlagen: %s", esp_err_to_name(ret));
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"NVS commit failed\"}");
        return ESP_OK;
    }

    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Servo-Konfiguration gespeichert\"}");
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/servo/position - Servo direkt auf Position fahren
// ============================================================================
static esp_err_t servo_position_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
    uint32_t pulse_us;

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }

    // JSON-Feld parsen
    if (!parse_json_number(body, "pulse_us", &pulse_us) ||
        pulse_us < 50 || pulse_us > 20000) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Ungueltiger pulse_us (50-20000us)\"}");
        return ESP_OK;
    }

    // Servo auf Position fahren mit FET-Aktivierung (konfigurierte Zeit)
    servo_set_position_with_fet(pulse_us, g_servo_config.fet_on_time_ms);

    ESP_LOGI(TAG, "Servo auf Position %lu us gefahren (FET %lu ms aktiviert)", pulse_us, g_servo_config.fet_on_time_ms);
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Servo-Position gesetzt (FET aktiviert)\"}");
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/wifi/reconnect - Verbindung neu aufbauen
// ============================================================================
static esp_err_t wifi_reconnect_handler(httpd_req_t *req)
{
    wifi_reconnect();
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Reconnect gestartet\"}");
    return ESP_OK;
}

// ============================================================================
// API Handler: POST /api/ota/rollback - Firmware-Rollback
// ============================================================================
static esp_err_t ota_rollback_handler(httpd_req_t *req)
{
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Rollback wird ausgeführt...\"}");
    ota_rollback();  // löst Neustart aus
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/stacks - Stack-Nutzung aller Tasks
// ============================================================================
static esp_err_t stacks_handler(httpd_req_t *req)
{
    // Statische Puffer verwenden (vermeidet Heap-Fragmentierung)
    uint8_t count = stack_monitor_get_all(stack_info_buffer, 16);
    int offset = snprintf(stack_json_buffer, sizeof(stack_json_buffer), "{\"stacks\":[");
    for (uint8_t i = 0; i < count; i++) {
        char esc[20] = {0};
        json_escape_string(stack_info_buffer[i].task_name, esc, sizeof(esc));
        if ((size_t)offset >= sizeof(stack_json_buffer) - 120) {
            break;
        }
        offset += snprintf(stack_json_buffer + offset, sizeof(stack_json_buffer) - offset,
            "%s{\"task\":\"%s\",\"free_bytes\":%lu,\"warning\":%s,\"critical\":%s}",
            (i > 0) ? "," : "",
            esc,
            (unsigned long)stack_info_buffer[i].stack_free,
            stack_info_buffer[i].warning ? "true" : "false",
            stack_info_buffer[i].critical ? "true" : "false");
    }
    snprintf(stack_json_buffer + offset, sizeof(stack_json_buffer) - offset, "],\"count\":%d}", count);
    
    send_json_response(req, stack_json_buffer);
    return ESP_OK;
}

// ============================================================================
// WiFi-Reset Handler: POST /api/wifi/reset
// ============================================================================
static esp_err_t wifi_reset_handler(httpd_req_t *req)
{
    esp_err_t ret = wifi_reset_credentials();
    if (ret == ESP_OK) {
        send_json_response(req, "{\"status\":\"OK\",\"message\":\"WiFi-Credentials gelöscht, AP-Modus aktiv\"}");
    } else {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"WiFi-Reset fehlgeschlagen\"}");
    }
    return ESP_OK;
}

// ============================================================================
// System-Reset Handler: POST /api/system/reset
// ============================================================================
static esp_err_t system_reset_handler(httpd_req_t *req)
{
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"System wird neu gestartet\"}");

    // Kurze Verzögerung damit Response gesendet werden kann
    vTaskDelay(pdMS_TO_TICKS(DELAY_500MS_MS));

    // System-Reset
    esp_restart();

    return ESP_OK;
}

// ============================================================================
// HTML Handler: GET / - Main Web UI (HTML aus eingebetteter Datei)
// ============================================================================
// Eingebettete HTML-Datei (siehe src/CMakeLists.txt EMBED_TXTFILES)
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    const size_t index_html_len = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html_start, index_html_len);
    return ESP_OK;
}

// ============================================================================
// Captive Portal Handler: Catch-All für alle Anfragen im AP-Modus
// ============================================================================
static esp_err_t captive_portal_handler(httpd_req_t *req)
{
    // Nur im AP-Modus weiterleiten
    if (!wifi_is_ap_mode_forced()) {
        // Nicht im AP-Modus: 404 zurückgeben
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    // Captive Portal Redirect: Auf Hauptseite weiterleiten
    char redirect_url[128];
    snprintf(redirect_url, sizeof(redirect_url), "http://%s/", WIFI_AP_IP);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", redirect_url);
    httpd_resp_send(req, NULL, 0);

    ESP_LOGI(TAG, "Captive Portal Redirect: %s -> %s", req->uri, redirect_url);
    return ESP_OK;
}

// ============================================================================
// iOS Captive Portal Handler: /hotspot-detect.html
// ============================================================================
static esp_err_t hotspot_detect_handler(httpd_req_t *req)
{
    // Auf Hauptseite weiterleiten
    char redirect_url[128];
    snprintf(redirect_url, sizeof(redirect_url), "http://%s/", WIFI_AP_IP);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", redirect_url);
    httpd_resp_send(req, NULL, 0);

    ESP_LOGI(TAG, "iOS Captive Portal Redirect: /hotspot-detect.html -> %s", redirect_url);
    return ESP_OK;
}

// ============================================================================
// URI Handler Registration
// ============================================================================
static httpd_uri_t status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
};

static httpd_uri_t errors_uri = {
    .uri = "/api/errors",
    .method = HTTP_GET,
    .handler = errors_handler,
    .user_ctx = NULL
};

static httpd_uri_t error_codes_uri = {
    .uri = "/api/error_codes",
    .method = HTTP_GET,
    .handler = error_codes_handler,
    .user_ctx = NULL
};

static httpd_uri_t errors_clear_uri = {
    .uri = "/api/errors/clear",
    .method = HTTP_POST,
    .handler = errors_clear_handler,
    .user_ctx = NULL
};

static httpd_uri_t ota_status_uri = {
    .uri = "/api/ota/status",
    .method = HTTP_GET,
    .handler = ota_status_handler,
    .user_ctx = NULL
};

static httpd_uri_t ota_start_uri = {
    .uri = "/api/ota/start",
    .method = HTTP_POST,
    .handler = ota_start_handler,
    .user_ctx = NULL
};

static httpd_uri_t ota_rollback_uri = {
    .uri = "/api/ota/rollback",
    .method = HTTP_POST,
    .handler = ota_rollback_handler,
    .user_ctx = NULL
};

static httpd_uri_t wifi_config_uri = {
    .uri = "/api/wifi",
    .method = HTTP_POST,
    .handler = wifi_config_handler,
    .user_ctx = NULL
};

static httpd_uri_t servo_config_get_uri = {
    .uri = "/api/servo_config",
    .method = HTTP_GET,
    .handler = servo_config_get_handler,
    .user_ctx = NULL
};

static httpd_uri_t servo_config_post_uri = {
    .uri = "/api/servo_config",
    .method = HTTP_POST,
    .handler = servo_config_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t servo_position_post_uri = {
    .uri = "/api/servo/position",
    .method = HTTP_POST,
    .handler = servo_position_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t wifi_reconnect_uri = {
    .uri = "/api/wifi/reconnect",
    .method = HTTP_POST,
    .handler = wifi_reconnect_handler,
    .user_ctx = NULL
};

static httpd_uri_t wifi_reset_uri = {
    .uri = "/api/wifi/reset",
    .method = HTTP_POST,
    .handler = wifi_reset_handler,
    .user_ctx = NULL
};

static httpd_uri_t system_reset_uri = {
    .uri = "/api/system/reset",
    .method = HTTP_POST,
    .handler = system_reset_handler,
    .user_ctx = NULL
};

// Telegram Handler
static esp_err_t telegram_config_get_handler(httpd_req_t *req)
{
    char response[512];
    bool configured = telegram_is_configured();
    bool enabled = telegram_is_enabled();
    int night_start = telegram_get_night_start_hour();
    int night_end = telegram_get_night_end_hour();

    snprintf(response, sizeof(response),
        "{\"configured\":%s,\"enabled\":%s,\"night_start_hour\":%d,\"night_end_hour\":%d}",
        configured ? "true" : "false",
        enabled ? "true" : "false",
        night_start,
        night_end);

    send_json_response(req, response);
    return ESP_OK;
}

static esp_err_t telegram_config_post_handler(httpd_req_t *req)
{
    char body[512] = {0};
    char bot_token[256] = {0};
    char chat_id[64] = {0};
    char night_start_str[8] = {0};
    char night_end_str[8] = {0};

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }

    // JSON-Felder parsen
    if (!parse_json_field(body, "bot_token", bot_token, sizeof(bot_token)) || strlen(bot_token) == 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Missing or invalid bot_token\"}");
        return ESP_OK;
    }

    if (!parse_json_field(body, "chat_id", chat_id, sizeof(chat_id)) || strlen(chat_id) == 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Missing or invalid chat_id\"}");
        return ESP_OK;
    }

    // Nachtzeiten parsen (optional)
    int night_start = -1;
    int night_end = -1;
    if (parse_json_field(body, "night_start_hour", night_start_str, sizeof(night_start_str))) {
        night_start = atoi(night_start_str);
    }
    if (parse_json_field(body, "night_end_hour", night_end_str, sizeof(night_end_str))) {
        night_end = atoi(night_end_str);
    }

    // In NVS speichern
    esp_err_t ret = telegram_save_token(bot_token);
    if (ret != ESP_OK) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to save bot token\"}");
        return ESP_OK;
    }

    ret = telegram_save_chat_id(chat_id);
    if (ret != ESP_OK) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to save chat ID\"}");
        return ESP_OK;
    }

    // Nachtzeiten speichern (falls angegeben)
    if (night_start >= 0 && night_start <= 23) {
        telegram_set_night_start_hour(night_start);
    }
    if (night_end >= 0 && night_end <= 23) {
        telegram_set_night_end_hour(night_end);
    }

    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Telegram configuration saved\"}");
    return ESP_OK;
}

static esp_err_t telegram_test_handler(httpd_req_t *req)
{
    if (!telegram_is_configured()) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Telegram not configured\"}");
        return ESP_OK;
    }

    esp_err_t ret = telegram_send_message("Test-Nachricht vom Katzenbrunnen");
    if (ret == ESP_OK) {
        send_json_response(req, "{\"status\":\"OK\",\"message\":\"Test message sent\"}");
    } else {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to send test message\"}");
    }
    return ESP_OK;
}

static esp_err_t telegram_enabled_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    bool enabled;

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }

    if (parse_json_number(body, "enabled", (uint32_t*)&enabled)) {
        esp_err_t ret = telegram_set_enabled(enabled);
        if (ret == ESP_OK) {
            send_json_response(req, "{\"status\":\"OK\",\"message\":\"Telegram enabled status updated\"}");
        } else {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to update enabled status\"}");
        }
    } else {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Missing or invalid enabled field\"}");
    }
    return ESP_OK;
}

static esp_err_t telegram_night_hours_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    int night_start = -1;
    int night_end = -1;

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)sizeof(body)) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid body\"}");
        return ESP_OK;
    }

    int received = httpd_req_recv(req, body, total_len);
    if (received <= 0) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to read body\"}");
        return ESP_OK;
    }

    bool has_start = parse_json_number(body, "night_start_hour", (uint32_t*)&night_start);
    bool has_end = parse_json_number(body, "night_end_hour", (uint32_t*)&night_end);

    if (!has_start && !has_end) {
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Missing night_start_hour or night_end_hour\"}");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (has_start) {
        if (night_start < 0 || night_start > 23) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid night_start_hour (must be 0-23)\"}");
            return ESP_OK;
        }
        ret = telegram_set_night_start_hour(night_start);
        if (ret != ESP_OK) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to save night_start_hour\"}");
            return ESP_OK;
        }
    }

    if (has_end) {
        if (night_end < 0 || night_end > 23) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Invalid night_end_hour (must be 0-23)\"}");
            return ESP_OK;
        }
        ret = telegram_set_night_end_hour(night_end);
        if (ret != ESP_OK) {
            send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Failed to save night_end_hour\"}");
            return ESP_OK;
        }
    }

    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Night hours updated\"}");
    return ESP_OK;
}

static httpd_uri_t telegram_config_get_uri = {
    .uri = "/api/telegram_config",
    .method = HTTP_GET,
    .handler = telegram_config_get_handler,
    .user_ctx = NULL
};

static httpd_uri_t telegram_config_post_uri = {
    .uri = "/api/telegram_config",
    .method = HTTP_POST,
    .handler = telegram_config_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t telegram_test_uri = {
    .uri = "/api/telegram/test",
    .method = HTTP_POST,
    .handler = telegram_test_handler,
    .user_ctx = NULL
};

static httpd_uri_t telegram_enabled_post_uri = {
    .uri = "/api/telegram/enabled",
    .method = HTTP_POST,
    .handler = telegram_enabled_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t telegram_night_hours_post_uri = {
    .uri = "/api/telegram/night_hours",
    .method = HTTP_POST,
    .handler = telegram_night_hours_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t stacks_uri = {
    .uri = "/api/stacks",
    .method = HTTP_GET,
    .handler = stacks_handler,
    .user_ctx = NULL
};

static httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
};

static httpd_uri_t captive_portal_uri = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static httpd_uri_t hotspot_detect_uri = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = hotspot_detect_handler,
    .user_ctx = NULL
};

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.lru_purge_enable = true;
    config.stack_size = TASK_STACK_WEB;     // httpd-Task-Stack vergrößern
    config.max_uri_handlers = 21;           // 20 Handler + Reserve (sonst scheitert Captive-Portal-Catch-All)
    config.uri_match_fn = httpd_uri_match_wildcard;  // Wildcard-Matching für Catch-All "/*" (Captive Portal)
    config.core_id = TASK_CORE_NETWORK;     // httpd auf Netzwerk-Core
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web-Server Start fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }
    
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &errors_uri);
    httpd_register_uri_handler(server, &error_codes_uri);
    httpd_register_uri_handler(server, &errors_clear_uri);
    httpd_register_uri_handler(server, &ota_status_uri);
    httpd_register_uri_handler(server, &ota_start_uri);
    httpd_register_uri_handler(server, &ota_rollback_uri);
    httpd_register_uri_handler(server, &wifi_config_uri);
    httpd_register_uri_handler(server, &servo_config_get_uri);
    httpd_register_uri_handler(server, &servo_config_post_uri);
    httpd_register_uri_handler(server, &servo_position_post_uri);
    httpd_register_uri_handler(server, &wifi_reconnect_uri);
    httpd_register_uri_handler(server, &wifi_reset_uri);
    httpd_register_uri_handler(server, &system_reset_uri);
    httpd_register_uri_handler(server, &telegram_config_get_uri);
    httpd_register_uri_handler(server, &telegram_config_post_uri);
    httpd_register_uri_handler(server, &telegram_test_uri);
    httpd_register_uri_handler(server, &telegram_enabled_post_uri);
    httpd_register_uri_handler(server, &telegram_night_hours_post_uri);
    httpd_register_uri_handler(server, &stacks_uri);
    httpd_register_uri_handler(server, &hotspot_detect_uri);  // iOS Captive Portal
    httpd_register_uri_handler(server, &captive_portal_uri);  // Muss zuletzt registriert werden (Catch-All)
    
    ESP_LOGI(TAG, "Web-Server gestartet auf Port %d", WEB_SERVER_PORT);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (server == NULL) {
        return web_server_init();
    }
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web-Server gestoppt");
    }
    return ESP_OK;
}
