#include "led.h"
#include "config.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led";

// WS2812B an GPIO_LED_DATA (GPIO48, fest verdrahtet) via RMT
#define LED_RESOLUTION_HZ  10000000   // 10 MHz -> 0,1 µs pro Tick

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

// Eine GRB-Farbe an die WS2812B senden
static void led_send_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_chan || !s_led_encoder) return;
    uint8_t grb[3] = { g, r, b };  // WS2812B erwartet GRB-Reihenfolge
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    rmt_transmit(s_led_chan, s_led_encoder, grb, sizeof(grb), &tx_config);
    rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY);
}

esp_err_t led_init(void)
{
#if LED_ENABLE
    ESP_LOGI(TAG, "Initialisiere WS2812B an GPIO%d", GPIO_LED_DATA);

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = GPIO_LED_DATA,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    // WS2812B-Bit-Timing bei 10 MHz: T0H=0,3µs T0L=0,8µs / T1H=0,7µs T1L=0,6µs
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 8 },
        .bit1 = { .level0 = 1, .duration0 = 7, .level1 = 0, .duration1 = 6 },
        .flags.msb_first = 1,
    };
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &s_led_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_enable(s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable fehlgeschlagen: %s", esp_err_to_name(ret));
        return ret;
    }

    led_set_state(LED_STATE_IDLE);  // Start: System bereit
#else
    ESP_LOGI(TAG, "LEDs deaktiviert (LED_ENABLE=false)");
#endif
    return ESP_OK;
}

void led_set_state(led_state_t state)
{
#if LED_ENABLE
    switch (state) {
        case LED_STATE_OFF:
            led_send_color(0, 0, 0);
            break;
        case LED_STATE_IDLE:
            led_send_color(0, 20, 0);    // grün gedimmt
            break;
        case LED_STATE_MOTION:
            led_send_color(40, 30, 0);   // gelb
            break;
        case LED_STATE_OPEN:
            led_send_color(0, 0, 60);    // blau
            break;
        case LED_STATE_CRITICAL:
            led_send_color(60, 0, 0);    // rot
            break;
        default:
            led_send_color(0, 0, 0);
            break;
    }
#endif
}
