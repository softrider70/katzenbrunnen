#include "telegram.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include <string.h>

static const char *TAG = "telegram";

// NVS-Schlüssel
#define NVS_TELEGRAM_NAMESPACE "telegram"
#define NVS_KEY_BOT_TOKEN "bot_token"
#define NVS_KEY_CHAT_ID "chat_id"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_NIGHT_START_HOUR "night_start_hour"
#define NVS_KEY_NIGHT_END_HOUR "night_end_hour"

// Bot API URL
#define TELEGRAM_API_URL "https://api.telegram.org/bot"
#define TELEGRAM_SEND_ENDPOINT "/sendMessage"

// Nacht-Modus Konfiguration
#define MAX_NIGHT_EVENTS 6
#define NIGHT_EVENT_MAX_LEN 32  // "HH:MM+Dauer" (z.B. "23:45+5s")

// Lokale Speicher für Token und Chat ID
static char g_bot_token[256] = {0};
static char g_chat_id[64] = {0};
static bool g_enabled = true;  // Standardmäßig aktiviert
static int g_night_start_hour = 23;  // Standard: 23 Uhr
static int g_night_end_hour = 8;    // Standard: 8 Uhr

// Nacht-Modus Puffer
static char g_night_buffer[MAX_NIGHT_EVENTS][NIGHT_EVENT_MAX_LEN] = {0};
static int g_night_buffer_count = 0;

/**
 * @brief HTTP Event Handler für Telegram API
 */
static esp_err_t telegram_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t telegram_init(void)
{
    esp_err_t ret;
    nvs_handle_t nvs_handle;

    // NVS öffnen
    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS Namespace nicht gefunden, verwende Defaults");
        return ESP_OK;
    }

    // Bot Token lesen
    size_t len = sizeof(g_bot_token);
    ret = nvs_get_str(nvs_handle, NVS_KEY_BOT_TOKEN, g_bot_token, &len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Bot Token geladen");
    } else {
        ESP_LOGW(TAG, "Bot Token nicht in NVS gefunden");
        memset(g_bot_token, 0, sizeof(g_bot_token));
    }

    // Chat ID lesen
    len = sizeof(g_chat_id);
    ret = nvs_get_str(nvs_handle, NVS_KEY_CHAT_ID, g_chat_id, &len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Chat ID geladen");
    } else {
        ESP_LOGW(TAG, "Chat ID nicht in NVS gefunden");
        memset(g_chat_id, 0, sizeof(g_chat_id));
    }

    // Enabled-Status lesen
    uint8_t enabled = 1;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_ENABLED, &enabled);
    if (ret == ESP_OK) {
        g_enabled = (enabled != 0);
        ESP_LOGI(TAG, "Telegram-Status geladen: %s", g_enabled ? "aktiviert" : "deaktiviert");
    } else {
        ESP_LOGI(TAG, "Enabled-Status nicht in NVS gefunden, Standard: aktiviert");
    }

    // Nacht-Startzeit lesen
    int8_t night_start = 23;
    ret = nvs_get_i8(nvs_handle, NVS_KEY_NIGHT_START_HOUR, &night_start);
    if (ret == ESP_OK) {
        g_night_start_hour = night_start;
        ESP_LOGI(TAG, "Nacht-Startzeit geladen: %d Uhr", g_night_start_hour);
    } else {
        ESP_LOGI(TAG, "Nacht-Startzeit nicht in NVS gefunden, Standard: 23 Uhr");
    }

    // Nacht-Stoppzeit lesen
    int8_t night_end = 8;
    ret = nvs_get_i8(nvs_handle, NVS_KEY_NIGHT_END_HOUR, &night_end);
    if (ret == ESP_OK) {
        g_night_end_hour = night_end;
        ESP_LOGI(TAG, "Nacht-Stoppzeit geladen: %d Uhr", g_night_end_hour);
    } else {
        ESP_LOGI(TAG, "Nacht-Stoppzeit nicht in NVS gefunden, Standard: 8 Uhr");
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

bool telegram_is_night_mode(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    return (hour >= g_night_start_hour || hour < g_night_end_hour);
}

void telegram_send_night_buffer(void)
{
    if (g_night_buffer_count == 0) {
        return;  // Keine gepufferten Nachrichten
    }

    if (!telegram_is_configured() || !g_enabled) {
        g_night_buffer_count = 0;  // Puffer leeren
        return;
    }

    // Zusammenfassende Nachricht erstellen
    char message[512];
    int pos = snprintf(message, sizeof(message), "🌙 Nacht-Ereignisse (%d):\n", g_night_buffer_count);
    
    for (int i = 0; i < g_night_buffer_count && pos < (int)sizeof(message) - 1; i++) {
        pos += snprintf(message + pos, sizeof(message) - pos, "%d. %s\n", i + 1, g_night_buffer[i]);
    }

    // Nachricht senden
    telegram_send_message(message);

    // Puffer leeren
    g_night_buffer_count = 0;
    memset(g_night_buffer, 0, sizeof(g_night_buffer));
}

esp_err_t telegram_send_message(const char *message)
{
    if (!g_enabled) {
        ESP_LOGD(TAG, "Telegram deaktiviert - Nachricht nicht gesendet");
        return ESP_OK;  // Kein Fehler, nur deaktiviert
    }

    if (!telegram_is_configured()) {
        ESP_LOGE(TAG, "Telegram nicht konfiguriert (Token oder Chat ID fehlt)");
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL || strlen(message) == 0) {
        ESP_LOGE(TAG, "Leere Nachricht");
        return ESP_ERR_INVALID_ARG;
    }

    // Nacht-Modus: Nachricht puffern statt senden
    if (telegram_is_night_mode()) {
        if (g_night_buffer_count < MAX_NIGHT_EVENTS) {
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            // Format: "HH:MM+Dauer" (Dauer aus Nachricht extrahieren)
            snprintf(g_night_buffer[g_night_buffer_count], NIGHT_EVENT_MAX_LEN,
                "%02d:%02d+%s", timeinfo.tm_hour, timeinfo.tm_min, message);
            g_night_buffer_count++;
            ESP_LOGI(TAG, "Nacht-Modus: Nachricht gepuffert (%d/%d)", g_night_buffer_count, MAX_NIGHT_EVENTS);
        } else {
            ESP_LOGW(TAG, "Nacht-Modus: Puffer voll, Nachricht ignoriert");
        }
        return ESP_OK;
    }

    // URL zusammenbauen
    char url[512];
    snprintf(url, sizeof(url), "%s%s%s", TELEGRAM_API_URL, g_bot_token, TELEGRAM_SEND_ENDPOINT);

    // JSON-Body manuell zusammenbauen (ohne cJSON)
    char json_body[1024];
    int json_len = snprintf(json_body, sizeof(json_body),
        "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
        g_chat_id, message);

    if (json_len >= (int)sizeof(json_body)) {
        ESP_LOGE(TAG, "JSON-Body zu lang");
        return ESP_ERR_NO_MEM;
    }

    // HTTP-Client Konfiguration mit TLS
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = telegram_http_event_handler,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP Client Initialisierung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    // Header setzen
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    // Request senden
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status);
        if (status == 200) {
            ESP_LOGI(TAG, "Nachricht erfolgreich gesendet");
        } else {
            ESP_LOGE(TAG, "Telegram API Fehler: Status %d", status);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST fehlgeschlagen: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t telegram_send_message_markdown(const char *message)
{
    if (!g_enabled) {
        ESP_LOGD(TAG, "Telegram deaktiviert - Nachricht nicht gesendet");
        return ESP_OK;  // Kein Fehler, nur deaktiviert
    }

    if (!telegram_is_configured()) {
        ESP_LOGE(TAG, "Telegram nicht konfiguriert (Token oder Chat ID fehlt)");
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL || strlen(message) == 0) {
        ESP_LOGE(TAG, "Leere Nachricht");
        return ESP_ERR_INVALID_ARG;
    }

    // URL zusammenbauen
    char url[512];
    snprintf(url, sizeof(url), "%s%s%s", TELEGRAM_API_URL, g_bot_token, TELEGRAM_SEND_ENDPOINT);

    // JSON-Body manuell zusammenbauen (ohne cJSON)
    char json_body[1024];
    int json_len = snprintf(json_body, sizeof(json_body),
        "{\"chat_id\":\"%s\",\"text\":\"%s\",\"parse_mode\":\"Markdown\"}",
        g_chat_id, message);

    if (json_len >= (int)sizeof(json_body)) {
        ESP_LOGE(TAG, "JSON-Body zu lang");
        return ESP_ERR_NO_MEM;
    }

    // HTTP-Client Konfiguration mit TLS
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = telegram_http_event_handler,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP Client Initialisierung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    // Header setzen
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    // Request senden
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status);
        if (status == 200) {
            ESP_LOGI(TAG, "Markdown-Nachricht erfolgreich gesendet");
        } else {
            ESP_LOGE(TAG, "Telegram API Fehler: Status %d", status);
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST fehlgeschlagen: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t telegram_save_token(const char *token)
{
    if (token == NULL || strlen(token) == 0) {
        ESP_LOGE(TAG, "Leerer Token");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(token) >= sizeof(g_bot_token)) {
        ESP_LOGE(TAG, "Token zu lang (max %d Zeichen)", sizeof(g_bot_token) - 1);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    nvs_handle_t nvs_handle;

    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen");
        return ret;
    }

    ret = nvs_set_str(nvs_handle, NVS_KEY_BOT_TOKEN, token);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            strncpy(g_bot_token, token, sizeof(g_bot_token) - 1);
            g_bot_token[sizeof(g_bot_token) - 1] = '\0';
            ESP_LOGI(TAG, "Bot Token gespeichert");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t telegram_save_chat_id(const char *chat_id)
{
    if (chat_id == NULL || strlen(chat_id) == 0) {
        ESP_LOGE(TAG, "Leere Chat ID");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(chat_id) >= sizeof(g_chat_id)) {
        ESP_LOGE(TAG, "Chat ID zu lang (max %d Zeichen)", sizeof(g_chat_id) - 1);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    nvs_handle_t nvs_handle;

    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen");
        return ret;
    }

    ret = nvs_set_str(nvs_handle, NVS_KEY_CHAT_ID, chat_id);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            strncpy(g_chat_id, chat_id, sizeof(g_chat_id) - 1);
            g_chat_id[sizeof(g_chat_id) - 1] = '\0';
            ESP_LOGI(TAG, "Chat ID gespeichert");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

bool telegram_is_configured(void)
{
    return (strlen(g_bot_token) > 0 && strlen(g_chat_id) > 0);
}

esp_err_t telegram_set_enabled(bool enabled)
{
    esp_err_t ret;
    nvs_handle_t nvs_handle;

    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen");
        return ret;
    }

    uint8_t enabled_val = enabled ? 1 : 0;
    ret = nvs_set_u8(nvs_handle, NVS_KEY_ENABLED, enabled_val);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            g_enabled = enabled;
            ESP_LOGI(TAG, "Telegram-Status gespeichert: %s", enabled ? "aktiviert" : "deaktiviert");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

bool telegram_is_enabled(void)
{
    return g_enabled;
}

esp_err_t telegram_set_night_start_hour(int hour)
{
    if (hour < 0 || hour > 23) {
        ESP_LOGE(TAG, "Ungültige Nacht-Startzeit: %d (muss 0-23 sein)", hour);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    nvs_handle_t nvs_handle;

    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen");
        return ret;
    }

    ret = nvs_set_i8(nvs_handle, NVS_KEY_NIGHT_START_HOUR, (int8_t)hour);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            g_night_start_hour = hour;
            ESP_LOGI(TAG, "Nacht-Startzeit gespeichert: %d Uhr", hour);
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t telegram_set_night_end_hour(int hour)
{
    if (hour < 0 || hour > 23) {
        ESP_LOGE(TAG, "Ungültige Nacht-Stoppzeit: %d (muss 0-23 sein)", hour);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    nvs_handle_t nvs_handle;

    ret = nvs_open(NVS_TELEGRAM_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS öffnen fehlgeschlagen");
        return ret;
    }

    ret = nvs_set_i8(nvs_handle, NVS_KEY_NIGHT_END_HOUR, (int8_t)hour);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            g_night_end_hour = hour;
            ESP_LOGI(TAG, "Nacht-Stoppzeit gespeichert: %d Uhr", hour);
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

int telegram_get_night_start_hour(void)
{
    return g_night_start_hour;
}

int telegram_get_night_end_hour(void)
{
    return g_night_end_hour;
}

void telegram_deinit(void)
{
    // Nichts zu tun - keine dynamischen Ressourcen
}
