#include "telegram.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>

static const char *TAG = "telegram";

// NVS-Schlüssel
#define NVS_TELEGRAM_NAMESPACE "telegram"
#define NVS_KEY_BOT_TOKEN "bot_token"
#define NVS_KEY_CHAT_ID "chat_id"

// Bot API URL
#define TELEGRAM_API_URL "https://api.telegram.org/bot"
#define TELEGRAM_SEND_ENDPOINT "/sendMessage"

// Lokale Speicher für Token und Chat ID
static char g_bot_token[256] = {0};
static char g_chat_id[64] = {0};

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

    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t telegram_send_message(const char *message)
{
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
        "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
        g_chat_id, message);

    if (json_len >= (int)sizeof(json_body)) {
        ESP_LOGE(TAG, "JSON-Body zu lang");
        return ESP_ERR_NO_MEM;
    }

    // HTTP-Client Konfiguration
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = telegram_http_event_handler,
        .timeout_ms = 5000,
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

    // HTTP-Client Konfiguration
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = telegram_http_event_handler,
        .timeout_ms = 5000,
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

void telegram_deinit(void)
{
    // Nichts zu tun - keine dynamischen Ressourcen
}
