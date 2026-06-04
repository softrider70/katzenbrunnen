idf_component_register(
    SRCS "main.c" "pir.c" "servo.c" "battery.c" "error_log.c" "stack_monitor.c" "watchdog.c" "wifi.c" "web_server.c" "ota.c" "dns_server.c"
    INCLUDE_DIRS "../../include"
    REQUIRES nvs_flash freertos driver esp_driver_gpio esp_adc esp_driver_ledc esp_timer esp_wifi esp_event esp_netif esp_http_server esp_http_client app_update mbedtls mdns
    EMBED_TXTFILES "web/index.html"
    WHOLE_ARCHIVE
)
