idf_component_register(
    SRCS "main.c" "pir.c" "servo.c" "battery.c" "error_log.c" "stack_monitor.c" "watchdog.c"
    INCLUDE_DIRS "../include"
    REQUIRES esp_system nvs_flash freertos esp_log driver gpio ledc adc esp_task_wdt
)
