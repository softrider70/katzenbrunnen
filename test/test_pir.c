#include "unity.h"
#include "pir.h"
#include "esp_log.h"

static const char *TAG = "TEST_PIR";

void setUp(void) {
    ESP_LOGI(TAG, "Setup PIR Test");
}

void tearDown(void) {
    ESP_LOGI(TAG, "Teardown PIR Test");
}

TEST_CASE("PIR Initialisierung", "[pir]")
{
    esp_err_t ret = pir_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("PIR Task Start", "[pir]")
{
    esp_err_t ret = pir_start_task();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("PIR Bewegungserkennung Status", "[pir]")
{
    bool motion = pir_motion_detected();
    // Initial sollte keine Bewegung erkannt werden
    TEST_ASSERT_FALSE(motion);
}

TEST_CASE("PIR Letzte Bewegungszeit", "[pir]")
{
    uint64_t last_motion = pir_get_last_motion_time();
    // Initial sollte 0 zurückgegeben werden
    TEST_ASSERT_EQUAL(0, last_motion);
}
