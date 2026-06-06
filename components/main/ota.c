#include "ota.h"
#include "config.h"
#include "error_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota";

// OTA-State
typedef struct {
    bool in_progress;
    bool last_result_ok;
    char phase[32];
    char message[64];
    char last_error[32];
    char current_version[32];
    char target_version[32];
    char url[OTA_URL_MAX_LEN];
    uint64_t last_start_ms;
    uint64_t last_end_ms;
    uint64_t boot_time_ms;
    bool health_check_passed;
} ota_state_t;

static ota_state_t ota_state = {
    .in_progress = false,
    .last_result_ok = false,
    .phase = "IDLE",
    .message = "",
    .last_error = "",
    .current_version = "",
    .target_version = "",
    .url = "",
    .last_start_ms = 0,
    .last_end_ms = 0,
    .boot_time_ms = 0,
    .health_check_passed = false
};

static SemaphoreHandle_t ota_mutex = NULL;
static TaskHandle_t ota_task_handle = NULL;
static TaskHandle_t ota_health_check_task_handle = NULL;

// Health-Check Task - markiert Firmware als valid nach erfolgreicher Laufzeit
static void ota_health_check_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Health-Check Task gestartet - warte %d ms vor Validierung", OTA_HEALTH_CHECK_DELAY_MS);
    
    // Warten bis System stabil läuft
    vTaskDelay(pdMS_TO_TICKS(OTA_HEALTH_CHECK_DELAY_MS));
    
    // Prüfen ob OTA-Partition im PENDING_VERIFY State
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t img_state;
    
    if (esp_ota_get_state_partition(running, &img_state) == ESP_OK) {
        if (img_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Markiere Firmware als valid (Rollback deaktiviert)");
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Firmware erfolgreich validiert");
                xSemaphoreTake(ota_mutex, portMAX_DELAY);
                ota_state.health_check_passed = true;
                xSemaphoreGive(ota_mutex);
            } else {
                ESP_LOGE(TAG, "Firmware-Validierung fehlgeschlagen: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "Firmware bereits validiert (State: %d)", img_state);
        }
    } else {
        ESP_LOGW(TAG, "OTA-State konnte nicht abgerufen werden");
    }
    
    ota_health_check_task_handle = NULL;
    vTaskDelete(NULL);
}

// OTA-Update Task (ESP-IDF 6.1 API)
static void ota_update_task(void *pvParameters)
{
    char *url = (char *)pvParameters;
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "OTA-Update gestartet: %s", url);
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    strncpy(ota_state.phase, "CONNECTING", sizeof(ota_state.phase) - 1);
    strncpy(ota_state.message, "Verbinde mit Server...", sizeof(ota_state.message) - 1);
    xSemaphoreGive(ota_mutex);
    
    // HTTP-Client Konfiguration
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = OTA_TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    
    // HTTPS OTA Konfiguration
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };
    
    // OTA beginnen
    esp_https_ota_handle_t https_ota_handle = NULL;
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin fehlgeschlagen: %s", esp_err_to_name(ret));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        ota_state.last_result_ok = false;
        strncpy(ota_state.phase, "FAILED", sizeof(ota_state.phase) - 1);
        snprintf(ota_state.message, sizeof(ota_state.message), "OTA Begin fehlgeschlagen: %s", esp_err_to_name(ret));
        strncpy(ota_state.last_error, esp_err_to_name(ret), sizeof(ota_state.last_error) - 1);
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        error_log_add(ERR_OTA_FAILURE, "ota_task", 2);
        free(url);
        ota_task_handle = NULL;
        vTaskDelete(NULL);
    }
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    strncpy(ota_state.phase, "DOWNLOADING", sizeof(ota_state.phase) - 1);
    strncpy(ota_state.message, "Lade Firmware...", sizeof(ota_state.message) - 1);
    xSemaphoreGive(ota_mutex);
    
    // OTA durchführen
    ret = esp_https_ota_perform(https_ota_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Perform fehlgeschlagen: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        ota_state.last_result_ok = false;
        strncpy(ota_state.phase, "FAILED", sizeof(ota_state.phase) - 1);
        snprintf(ota_state.message, sizeof(ota_state.message), "OTA Download fehlgeschlagen: %s", esp_err_to_name(ret));
        strncpy(ota_state.last_error, esp_err_to_name(ret), sizeof(ota_state.last_error) - 1);
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        error_log_add(ERR_OTA_FAILURE, "ota_task", 2);
        free(url);
        ota_task_handle = NULL;
        vTaskDelete(NULL);
    }
    
    // Prüfen ob alle Daten empfangen wurden
    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ESP_LOGE(TAG, "Komplette Daten wurden nicht empfangen");
        esp_https_ota_abort(https_ota_handle);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        ota_state.last_result_ok = false;
        strncpy(ota_state.phase, "FAILED", sizeof(ota_state.phase) - 1);
        strncpy(ota_state.message, "Unvollständige Daten empfangen", sizeof(ota_state.message) - 1);
        strncpy(ota_state.last_error, "incomplete-data", sizeof(ota_state.last_error) - 1);
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        error_log_add(ERR_OTA_FAILURE, "ota_task", 2);
        free(url);
        ota_task_handle = NULL;
        vTaskDelete(NULL);
    }
    
    // OTA abschließen
    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ret == ESP_OK && ota_finish_err == ESP_OK) {
        ESP_LOGI(TAG, "OTA erfolgreich abgeschlossen. Neustart...");
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        ota_state.last_result_ok = true;
        strncpy(ota_state.phase, "SUCCESS", sizeof(ota_state.phase) - 1);
        strncpy(ota_state.message, "OTA erfolgreich, Neustart...", sizeof(ota_state.message) - 1);
        ota_state.last_error[0] = '\0';
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        
        free(url);
        ota_task_handle = NULL;
        
        // Kurze Verzögerung für Logging
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Finish fehlgeschlagen: %s", esp_err_to_name(ota_finish_err));
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        ota_state.last_result_ok = false;
        strncpy(ota_state.phase, "FAILED", sizeof(ota_state.phase) - 1);
        snprintf(ota_state.message, sizeof(ota_state.message), "OTA Finish fehlgeschlagen: %s", esp_err_to_name(ota_finish_err));
        strncpy(ota_state.last_error, esp_err_to_name(ota_finish_err), sizeof(ota_state.last_error) - 1);
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        error_log_add(ERR_OTA_FAILURE, "ota_task", 2);
        free(url);
        ota_task_handle = NULL;
        vTaskDelete(NULL);
    }
}

esp_err_t ota_init(void)
{
    ota_mutex = xSemaphoreCreateMutex();
    if (ota_mutex == NULL) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    
    ota_state.boot_time_ms = (uint64_t)(esp_timer_get_time() / 1000);
    snprintf(ota_state.current_version, sizeof(ota_state.current_version), "%s", APP_VERSION);
    
    ESP_LOGI(TAG, "OTA-Modul initialisiert (Version: %s)", APP_VERSION);
    return ESP_OK;
}

esp_err_t ota_start_task(void)
{
#if OTA_HEALTH_CHECK_ENABLED
    // Health-Check Task starten (wenn aktiviert)
    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_health_check_task,
        "ota_health_check",
        2048,
        NULL,
        1,
        &ota_health_check_task_handle,
        TASK_CORE_CONTROL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Fehler beim Erstellen des Health-Check-Tasks");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Health-Check Task gestartet (Delay: %d ms)", OTA_HEALTH_CHECK_DELAY_MS);
#else
    ESP_LOGI(TAG, "Health-Check deaktiviert (OTA_HEALTH_CHECK_ENABLED=false)");
#endif
    
    return ESP_OK;
}

esp_err_t ota_start_update(const char *url)
{
    if (ota_mutex == NULL) {
        return ESP_FAIL;
    }
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    bool busy = ota_state.in_progress || (ota_task_handle != NULL);
    xSemaphoreGive(ota_mutex);
    if (busy) {
        ESP_LOGW(TAG, "OTA bereits aktiv");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (strlen(url) >= OTA_URL_MAX_LEN) {
        ESP_LOGE(TAG, "URL zu lang");
        return ESP_ERR_INVALID_ARG;
    }
    
    char *task_url = strdup(url);
    if (task_url == NULL) {
        ESP_LOGE(TAG, "Speicher-Allokation fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_state.in_progress = true;
    ota_state.last_result_ok = false;
    strncpy(ota_state.phase, "STARTING", sizeof(ota_state.phase) - 1);
    strncpy(ota_state.message, "OTA wird gestartet", sizeof(ota_state.message) - 1);
    ota_state.last_error[0] = '\0';
    strncpy(ota_state.url, url, sizeof(ota_state.url) - 1);
    ota_state.last_start_ms = (uint64_t)(esp_timer_get_time() / 1000);
    xSemaphoreGive(ota_mutex);
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_update_task,
        "ota_task",
        TASK_STACK_OTA,
        task_url,
        TASK_PRIO_OTA,
        &ota_task_handle,
        TASK_CORE_NETWORK
    );
    
    if (ret != pdPASS) {
        free(task_url);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_state.in_progress = false;
        strncpy(ota_state.phase, "FAILED", sizeof(ota_state.phase) - 1);
        strncpy(ota_state.message, "OTA Task konnte nicht gestartet werden", sizeof(ota_state.message) - 1);
        strncpy(ota_state.last_error, "task-create-failed", sizeof(ota_state.last_error) - 1);
        ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
        xSemaphoreGive(ota_mutex);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "OTA-Update gestartet: %s", url);
    return ESP_OK;
}

esp_err_t ota_get_status(bool *in_progress, bool *last_result_ok, char *phase, char *message)
{
    if (ota_mutex == NULL) {
        return ESP_FAIL;
    }
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    if (in_progress) *in_progress = ota_state.in_progress;
    if (last_result_ok) *last_result_ok = ota_state.last_result_ok;
    if (phase) strncpy(phase, ota_state.phase, 31);
    if (message) strncpy(message, ota_state.message, 63);
    xSemaphoreGive(ota_mutex);
    
    return ESP_OK;
}

esp_err_t ota_rollback(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Rollback durchführen
            ESP_LOGI(TAG, "Führe Rollback durch");
            esp_ota_mark_app_invalid_rollback_and_reboot();
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Kein Rollback möglich (kein pending verify state)");
    return ESP_ERR_INVALID_STATE;
}

void ota_deinit(void)
{
    // Mutex löschen
    if (ota_mutex != NULL) {
        vSemaphoreDelete(ota_mutex);
        ota_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "OTA-Modul deinitialisiert");
}
