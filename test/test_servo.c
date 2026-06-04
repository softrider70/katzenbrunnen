#include "unity.h"
#include "servo.h"
#include "esp_log.h"

static const char *TAG = "TEST_SERVO";

void setUp(void) {
    ESP_LOGI(TAG, "Setup Servo Test");
}

void tearDown(void) {
    ESP_LOGI(TAG, "Teardown Servo Test");
}

TEST_CASE("Servo Initialisierung", "[servo]")
{
    esp_err_t ret = servo_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

TEST_CASE("Servo Position setzen", "[servo]")
{
    servo_set_position(1500); // Neutralposition
    // Kein Rückgabewert, nur prüfen ob kein Crash
    TEST_ASSERT_TRUE(true);
}

TEST_CASE("Servo Wasserhahn öffnen", "[servo]")
{
    servo_open_valve();
    bool is_open = servo_is_valve_open();
    TEST_ASSERT_TRUE(is_open);
}

TEST_CASE("Servo Wasserhahn schließen", "[servo]")
{
    servo_close_valve();
    bool is_open = servo_is_valve_open();
    TEST_ASSERT_FALSE(is_open);
}

TEST_CASE("Servo Emergency Close", "[servo]")
{
    servo_emergency_close();
    bool is_open = servo_is_valve_open();
    TEST_ASSERT_FALSE(is_open);
}
