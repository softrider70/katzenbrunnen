#include "unity.h"
#include "battery.h"
#include "esp_log.h"

static const char *TAG = "TEST_BATTERY";

void setUp(void) {
    ESP_LOGI(TAG, "Setup Battery Test");
}

void tearDown(void) {
    ESP_LOGI(TAG, "Teardown Battery Test");
}

TEST_CASE("Battery Initialisierung", "[battery]")
{
    esp_err_t ret = battery_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("Battery Spannung abrufen", "[battery]")
{
    float voltage = battery_get_voltage();
    // Spannung sollte im realistischen Bereich liegen (2S LiPo: 6.0-8.4V)
    TEST_ASSERT_FLOAT_WITHIN(0.1, 7.4, voltage);
}

TEST_CASE("Battery Prozent abrufen", "[battery]")
{
    uint8_t percent = battery_get_percent();
    // Prozent sollte zwischen 0 und 100 liegen
    TEST_ASSERT_TRUE(percent >= 0 && percent <= 100);
}

TEST_CASE("Battery kritischer Status", "[battery]")
{
    bool critical = battery_is_critical();
    // Initial sollte nicht kritisch sein
    TEST_ASSERT_FALSE(critical);
}

TEST_CASE("Battery Task Start", "[battery]")
{
    esp_err_t ret = battery_start_task();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}
