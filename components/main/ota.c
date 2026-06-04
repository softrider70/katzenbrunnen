#include "ota.h"
#include "config.h"
#include "error_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota";

// OTA-State (stubs für ESP-IDF 6.1 Kompatibilität)
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

// OTA-Update Task (Stub - ESP-IDF 6.1 API benötigt Rewrite)
static void ota_update_task(void *pvParameters)
{
    char *url = (char *)pvParameters;
    ESP_LOGW(TAG, "OTA ist in ESP-IDF 6.1 deaktiviert (API-Änderungen). URL: %s", url);
    
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_state.in_progress = false;
    ota_state.last_result_ok = false;
    strncpy(ota_state.phase, "DISABLED", sizeof(ota_state.phase) - 1);
    strncpy(ota_state.message, "OTA in ESP-IDF 6.1 deaktiviert", sizeof(ota_state.message) - 1);
    strncpy(ota_state.last_error, "ESP_ERR_NOT_SUPPORTED", sizeof(ota_state.last_error) - 1);
    ota_state.last_end_ms = (uint64_t)(esp_timer_get_time() / 1000);
    xSemaphoreGive(ota_mutex);
    
    free(url);
    ota_task_handle = NULL;
    vTaskDelete(NULL);
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
    
    ESP_LOGI(TAG, "OTA-Modul initialisiert (Version: %s) - DEAKTIVIERT für ESP-IDF 6.1", APP_VERSION);
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
    ESP_LOGW(TAG, "OTA Rollback ist in ESP-IDF 6.1 deaktiviert");
    return ESP_ERR_NOT_SUPPORTED;
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
