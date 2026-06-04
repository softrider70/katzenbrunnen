#include "web_server.h"
#include "config.h"
#include "error_log.h"
#include "wifi.h"
#include "battery.h"
#include "servo.h"
#include "pir.h"
#include "ota.h"
#include "stack_monitor.h"
#include "heap_monitor.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>

static const char *TAG = "web_server";
static httpd_handle_t server = NULL;

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
    
    float batt_voltage = battery_get_voltage();
    uint8_t batt_percent = battery_get_percent();
    bool batt_critical = battery_is_critical();
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
        "\"battery_voltage\":%.2f,"
        "\"battery_percent\":%d,"
        "\"battery_critical\":%s,"
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
        "\"uptime_ms\":%llu"
        "}",
        valve_open ? "OPEN" : "CLOSED",
        valve_open ? "true" : "false",
        motion_detected ? "true" : "false",
        batt_voltage,
        batt_percent,
        batt_critical ? "true" : "false",
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
        (unsigned long long)(esp_timer_get_time() / 1000)
    );
    
    send_json_response(req, json_response);
    return ESP_OK;
}

// ============================================================================
// API Handler: GET /api/errors - Error Log (FIFO)
// ============================================================================
static esp_err_t errors_handler(httpd_req_t *req)
{
    // Speicher auf dem Heap statt Stack (httpd-Task-Stack ist begrenzt)
    const size_t entries_size = sizeof(error_log_entry_t) * ERROR_LOG_MAX_ENTRIES;
    const size_t json_size = 8192;
    
    error_log_entry_t *entries = malloc(entries_size);
    char *json_response = malloc(json_size);
    if (entries == NULL || json_response == NULL) {
        free(entries);
        free(json_response);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Out of memory\"}");
        return ESP_OK;
    }
    
    uint16_t count = error_log_get_all(entries, ERROR_LOG_MAX_ENTRIES);
    
    int offset = snprintf(json_response, json_size, "{\"errors\":[");
    
    for (uint16_t i = 0; i < count; i++) {
        char escaped_task[32] = {0};
        json_escape_string(entries[i].task_name, escaped_task, sizeof(escaped_task));
        
        // Pufferüberlauf vermeiden: vor jedem Eintrag Restplatz prüfen
        if ((size_t)offset >= json_size - 160) {
            break;
        }
        
        offset += snprintf(json_response + offset, json_size - offset,
            "%s{\"code\":\"%s\",\"id\":%d,\"timestamp_ms\":%llu,\"task\":\"%s\",\"severity\":%d}",
            (i > 0) ? "," : "",
            entries[i].code,
            entries[i].error_id,
            (unsigned long long)entries[i].timestamp_ms,
            escaped_task,
            entries[i].severity
        );
    }
    
    snprintf(json_response + offset, json_size - offset, "],\"count\":%d}", count);
    
    send_json_response(req, json_response);
    
    free(entries);
    free(json_response);
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
// API Handler: POST /api/valve/toggle - Toggle Water Valve
// ============================================================================
static esp_err_t valve_toggle_handler(httpd_req_t *req)
{
    char response[128];
    
    if (servo_is_valve_open()) {
        servo_close_valve();
        snprintf(response, sizeof(response), "{\"status\":\"OK\",\"message\":\"Valve closed\"}");
    } else {
        servo_open_valve();
        snprintf(response, sizeof(response), "{\"status\":\"OK\",\"message\":\"Valve opened\"}");
    }
    
    send_json_response(req, response);
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
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"OTA start failed\"}");
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
    stack_info_t *info = malloc(sizeof(stack_info_t) * 16);
    char *json = malloc(2048);
    if (info == NULL || json == NULL) {
        free(info);
        free(json);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Out of memory\"}");
        return ESP_OK;
    }
    
    uint8_t count = stack_monitor_get_all(info, 16);
    int offset = snprintf(json, 2048, "{\"stacks\":[");
    for (uint8_t i = 0; i < count; i++) {
        char esc[20] = {0};
        json_escape_string(info[i].task_name, esc, sizeof(esc));
        if ((size_t)offset >= 2048 - 120) {
            break;
        }
        offset += snprintf(json + offset, 2048 - offset,
            "%s{\"task\":\"%s\",\"free_bytes\":%lu,\"warning\":%s,\"critical\":%s}",
            (i > 0) ? "," : "",
            esc,
            (unsigned long)info[i].stack_free,
            info[i].warning ? "true" : "false",
            info[i].critical ? "true" : "false");
    }
    snprintf(json + offset, 2048 - offset, "],\"count\":%d}", count);
    
    send_json_response(req, json);
    free(info);
    free(json);
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
    vTaskDelay(pdMS_TO_TICKS(500));

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
// PIR Events Handler: GET /api/pir_events
// ============================================================================
static esp_err_t pir_events_handler(httpd_req_t *req)
{
    pir_event_t *events = malloc(sizeof(pir_event_t) * PIR_EVENT_MAX_COUNT);
    char *json_response = malloc(8192);
    if (events == NULL || json_response == NULL) {
        free(events);
        free(json_response);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Out of memory\"}");
        return ESP_OK;
    }

    uint16_t count = pir_get_events(events, PIR_EVENT_MAX_COUNT);

    int offset = snprintf(json_response, 8192, "{\"events\":[");

    for (uint16_t i = 0; i < count; i++) {
        if ((size_t)offset >= 8192 - 80) {
            break;
        }

        // Zeitstempel in lesbares Format umwandeln
        time_t timestamp_sec = events[i].timestamp_ms / 1000;
        struct tm *tm_info = localtime(&timestamp_sec);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        offset += snprintf(json_response + offset, 8192 - offset,
            "%s{\"timestamp_ms\":%llu,\"timestamp\":\"%s\",\"duration_ms\":%u}",
            (i > 0) ? "," : "",
            (unsigned long long)events[i].timestamp_ms,
            time_str,
            events[i].duration_ms
        );
    }

    snprintf(json_response + offset, 8192 - offset, "],\"count\":%d}", count);

    send_json_response(req, json_response);

    free(events);
    free(json_response);
    return ESP_OK;
}

// ============================================================================
// PIR Events Clear Handler: POST /api/pir_events/clear
// ============================================================================
static esp_err_t pir_events_clear_handler(httpd_req_t *req)
{
    pir_clear_events();
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"PIR-Ereignisse gelöscht\"}");
    return ESP_OK;
}

// ============================================================================
// Servo Events Handler: GET /api/servo_events
// ============================================================================
static esp_err_t servo_events_handler(httpd_req_t *req)
{
    servo_event_t *events = malloc(sizeof(servo_event_t) * SERVO_EVENT_MAX_COUNT);
    char *json_response = malloc(8192);
    if (events == NULL || json_response == NULL) {
        free(events);
        free(json_response);
        send_json_response(req, "{\"status\":\"ERROR\",\"message\":\"Out of memory\"}");
        return ESP_OK;
    }

    uint16_t count = servo_get_events(events, SERVO_EVENT_MAX_COUNT);

    int offset = snprintf(json_response, 8192, "{\"events\":[");

    for (uint16_t i = 0; i < count; i++) {
        if ((size_t)offset >= 8192 - 80) {
            break;
        }

        // Zeitstempel in lesbares Format umwandeln
        time_t timestamp_sec = events[i].timestamp_ms / 1000;
        struct tm *tm_info = localtime(&timestamp_sec);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        offset += snprintf(json_response + offset, 8192 - offset,
            "%s{\"timestamp_ms\":%llu,\"timestamp\":\"%s\",\"duration_ms\":%u}",
            (i > 0) ? "," : "",
            (unsigned long long)events[i].timestamp_ms,
            time_str,
            events[i].duration_ms
        );
    }

    snprintf(json_response + offset, 8192 - offset, "],\"count\":%d}", count);

    send_json_response(req, json_response);

    free(events);
    free(json_response);
    return ESP_OK;
}

// ============================================================================
// Servo Events Clear Handler: POST /api/servo_events/clear
// ============================================================================
static esp_err_t servo_events_clear_handler(httpd_req_t *req)
{
    servo_clear_events();
    send_json_response(req, "{\"status\":\"OK\",\"message\":\"Öffnungsereignisse gelöscht\"}");
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

static httpd_uri_t valve_toggle_uri = {
    .uri = "/api/valve/toggle",
    .method = HTTP_POST,
    .handler = valve_toggle_handler,
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

static httpd_uri_t stacks_uri = {
    .uri = "/api/stacks",
    .method = HTTP_GET,
    .handler = stacks_handler,
    .user_ctx = NULL
};

static httpd_uri_t pir_events_uri = {
    .uri = "/api/pir_events",
    .method = HTTP_GET,
    .handler = pir_events_handler,
    .user_ctx = NULL
};

static httpd_uri_t pir_events_clear_uri = {
    .uri = "/api/pir_events/clear",
    .method = HTTP_POST,
    .handler = pir_events_clear_handler,
    .user_ctx = NULL
};

static httpd_uri_t servo_events_uri = {
    .uri = "/api/servo_events",
    .method = HTTP_GET,
    .handler = servo_events_handler,
    .user_ctx = NULL
};

static httpd_uri_t servo_events_clear_uri = {
    .uri = "/api/servo_events/clear",
    .method = HTTP_POST,
    .handler = servo_events_clear_handler,
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

esp_err_t web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.lru_purge_enable = true;
    config.stack_size = TASK_STACK_WEB;     // httpd-Task-Stack vergrößern
    config.max_uri_handlers = 16;           // Genug Platz für alle Endpunkte
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
    httpd_register_uri_handler(server, &valve_toggle_uri);
    httpd_register_uri_handler(server, &errors_clear_uri);
    httpd_register_uri_handler(server, &ota_status_uri);
    httpd_register_uri_handler(server, &ota_start_uri);
    httpd_register_uri_handler(server, &ota_rollback_uri);
    httpd_register_uri_handler(server, &wifi_config_uri);
    httpd_register_uri_handler(server, &wifi_reconnect_uri);
    httpd_register_uri_handler(server, &wifi_reset_uri);
    httpd_register_uri_handler(server, &system_reset_uri);
    httpd_register_uri_handler(server, &stacks_uri);
    httpd_register_uri_handler(server, &pir_events_uri);
    httpd_register_uri_handler(server, &pir_events_clear_uri);
    httpd_register_uri_handler(server, &servo_events_uri);
    httpd_register_uri_handler(server, &servo_events_clear_uri);
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
