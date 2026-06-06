#ifndef CONFIG_H
#define CONFIG_H

#include "sdkconfig.h"

// ============================================================================
// GPIO Pin Configuration - Katzenbrunnen ESP32-S3
// ============================================================================
#define GPIO_PIR_SENSOR  4   // PIR Bewegungssensor (BIS0001) - RTC GPIO für Deep Sleep Wake-Up
#define GPIO_LED_DATA    48  // WS2812B RGB LED (fest verdrahtet auf GPIO48)
#define GPIO_BUTTON      10  // manueller Taster
#define GPIO_SERVO       11  // Gigaline Standard Servo (Wasserhahn-Steuerung)
#define GPIO_SERVO_ENABLE 5  // 2N7000 MOSFET Gate (Low-Side-Switching für Servo-Stromversorgung)
#define GPIO_BATTERY_ADC 1   // ADC1_CH1 - Batteriespannungsmessung

// ============================================================================
// LED Configuration
// ============================================================================
#define LED_ENABLE false  // LEDs aktivieren (true) oder deaktivieren (false)

// ============================================================================
// Katzenbrunnen Parameter
// ============================================================================
#define SERVO_OPEN_ANGLE_US      120    // Servo-Position für geöffneten Wasserhahn (Pulsweite in µs)
#define SERVO_CLOSE_ANGLE_US     750    // Servo-Position für geschlossenen Wasserhahn (Pulsweite in µs)
#define CLOSE_TIMEOUT_MS         8000   // Timeout ohne HIGH-Signal vor Schließen (ms) - Default 8s
#define MIN_MOTION_DURATION_MS  10000   // Minimale Bewegungsdauer für Aktivierung (ms) - für pulsierendes PIR-Signal
#define PIR_COOLDOWN_MS         30000   // Cooldown nach Schließen vor erneuter Aktivierung (ms)
#define PIR_MOTION_TIMEOUT_MS   10000   // Timeout ohne HIGH-Signal -> Objekt weg (ms)

// ============================================================================
// Batterie-Konfiguration (LiPo 2S, 7.4V nominal, 2Ah)
// ============================================================================
// WICHTIG (Hardware): Bei 2S liegt die Packspannung bei bis zu 8.4V. Der
// Spannungsteiler muss so dimensioniert sein, dass am ADC max. ~3.0V anliegen.
// Mit R1=22k / R2=10k ergibt sich Faktor 3.2 -> 8.4V/3.2 = 2.625V (sicher).
#define BATTERY_CELLS          2       // Anzahl Zellen in Serie (2S)
#define BATTERY_CAPACITY_AH     2.0     // Kapazität in Ah
#define BATTERY_VOLTAGE_MIN     3.0     // Minimale Zellspannung (V) - Abschaltung
#define BATTERY_VOLTAGE_MAX     4.2     // Maximale Zellspannung (V) - voll geladen
#define BATTERY_VOLTAGE_NOMINAL 3.7     // Nominale Zellspannung (V)
#define ADC_ATTENUATION        ADC_ATTEN_DB_12  // ADC Dämpfung (~0-3.1V Messbereich)
#define ADC_UNIT               ADC_UNIT_1
#define ADC_CHANNEL            ADC_CHANNEL_0   // GPIO1 = ADC1_CH0 auf ESP32-S3
#define BATTERY_DIVIDER_R1     22000   // Spannungsteiler R1 (Ohm) - oben
#define BATTERY_DIVIDER_R2     10000   // Spannungsteiler R2 (Ohm) - unten (an ADC)

// ============================================================================
// Servo Configuration - Gigaline Standard Servo
// ============================================================================
// Abmessungen: 39.7mm x 20.37mm x 36.12mm (h)
// Typ: Standard-Servo (Futaba S3003 kompatibel)
// Gewicht: ~37g (basierend auf S3003 Vergleich)
// Getriebe: Metallgetriebe (MG - Metal Gear)
// Lagerung: Kugellager (BB - Ball Bearing)
// Betriebsspannung: 4.8-6.0V
// Drehmoment: ~3.0kg/cm bei 4.8V, ~3.7kg/cm bei 6.0V (geschätzt)
// Geschwindigkeit: ~0.19s/60° bei 4.8V (geschätzt)
// Verdrehung: ~180°
#define SERVO_MIN_PULSE_US   500    // Minimale Pulsweite (0.5ms)
#define SERVO_MAX_PULSE_US   2400   // Maximale Pulsweite (2.4ms)
#define SERVO_NEUTRAL_US     1500   // Neutralposition (1.5ms)
#define SERVO_FREQUENCY_HZ   50     // PWM Frequenz (50Hz = 20ms Periode)
#define SERVO_VOLTAGE_MIN    4.8    // Minimale Betriebsspannung (V)
#define SERVO_VOLTAGE_MAX    6.0    // Maximale Betriebsspannung (V)
#define SERVO_TORQUE_4V8     3.0    // Drehmoment bei 4.8V (kg/cm)
#define SERVO_TORQUE_6V      3.7    // Drehmoment bei 6.0V (kg/cm)
#define SERVO_SPEED_4V8      0.19   // Geschwindigkeit bei 4.8V (s/60°)
#define SERVO_WEIGHT_G       37     // Gewicht (Gramm)

// ============================================================================
// PIR Sensor Configuration - BIS0001 PIR Motion Detector
// ============================================================================
// Modul: Elegoo 37-in-1 Sensor Kit PIR Modul
// Chip: BIS0001 (Micro Power PIR Motion Detector IC)
// Abmessungen: 24mm x 33mm
// Anschlüsse: GND, VCC, OUT
// Betriebsspannung: 3.3V - 5.0V
// Stromaufnahme: ~10mA bei 5V
// Betriebstemperatur: -20°C bis +70°C
// Erfassungsbereich: bis 6m (20 feet)
// Erfassungswinkel: 110° x 70°
// Ausgang: Digital (HIGH bei Bewegung, LOW im Ruhezustand)
// Verzögerungszeit: einstellbar via Potentiometer
// Trigger-Modus: Repeatable/Non-repeatable (jumper-selectable)
#define PIR_VOLTAGE_MIN      3.3    // Minimale Betriebsspannung (V)
#define PIR_VOLTAGE_MAX      5.0    // Maximale Betriebsspannung (V)
#define PIR_CURRENT_MA       10     // Stromaufnahme bei 5V (mA)
#define PIR_DETECTION_RANGE_M 6.0   // Maximale Erfassungsreichweite (m)
#define PIR_DETECTION_ANGLE_H 110   // Horizontaler Erfassungswinkel (Grad)
#define PIR_DETECTION_ANGLE_V 70    // Vertikaler Erfassungswinkel (Grad)
#define PIR_OUTPUT_HIGH     3.0    // Ausgangsspannung HIGH (V)

// ============================================================================
// FreeRTOS Configuration
// ============================================================================
// Task-Stackgrößen und Prioritäten werden direkt hier definiert

// Task Stack Sizes
#define TASK_STACK_PIR          4096    // PIR-Task Stack
#define TASK_STACK_SERVO        4096    // Servo-Task Stack (auch Control-Task)
#define TASK_STACK_WEB          8192    // Web-Server-Task Stack (httpd)
#define TASK_STACK_WIFI         4096    // WiFi-Task Stack
#define TASK_STACK_OTA          8192    // OTA-Task Stack
#define TASK_STACK_BATTERY      3072    // Battery-Monitor-Task Stack
#define TASK_STACK_APP          4096    // Hauptanwendungs-Task Stack
#define TASK_STACK_CONTROL      4096    // Steuerungs-Task Stack

// Task Priorities (0-24, higher = more important)
#define TASK_PRIO_PIR           5       // PIR-Task Priorität
#define TASK_PRIO_SERVO         4       // Servo-Task Priorität
#define TASK_PRIO_CONTROL       4       // Steuerungs-Task Priorität
#define TASK_PRIO_WEB           3       // Web-Server-Task Priorität
#define TASK_PRIO_WIFI          2       // WiFi-Task Priorität
#define TASK_PRIO_OTA           1       // OTA-Task Priorität
#define TASK_PRIO_BATTERY       2       // Battery-Monitor-Task Priorität
#define TASK_PRIO_APP           3       // Hauptanwendungs-Task Priorität

// Task Core Affinity
#define TASK_CORE_CONTROL        0       // Core 0 für Steuerungsaufgaben
#define TASK_CORE_NETWORK       1       // Core 1 für Netzwerkaufgaben

// ============================================================================
// WiFi Configuration
// ============================================================================
#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64
#define WIFI_RETRY_INTERVAL_MS  5000    // WiFi Verbindungsretry Intervall
#define WIFI_MAX_RETRY           3       // Maximale Verbindungsversuche vor AP-Start
#define WIFI_AP_SSID            "katzenbrunnen_setup"
#define WIFI_AP_PASSWORD        ""             // leer = offenes Setup-Netz (kein Passwort nötig)
#define WIFI_AP_IP              "192.168.4.1"  // AP IP-Adresse
#define WIFI_AP_GATEWAY         "192.168.4.1"  // AP Gateway
#define WIFI_AP_NETMASK         "255.255.255.0"  // AP Netmask
#define WIFI_HOSTNAME           "katzenbrunnen"  // mDNS Hostname
#define WIFI_TX_POWER           14  // WiFi Sendeleistung in dBm (0-20, Standard: 14 für 7m Entfernung)
#define WIFI_TX_POWER_HW_MAX    84  // TX-Power Hardware Limit (ESP32-S3)

// ============================================================================
// Web Server Configuration
// ============================================================================
#define WEB_SERVER_PORT         80
// MAX_EVENT_LOG_ENTRIES und MAX_OPEN_TIME_ENTRIES werden aktuell nicht verwendet
// #define MAX_EVENT_LOG_ENTRIES   50
// #define MAX_OPEN_TIME_ENTRIES   50

// ============================================================================
// OTA Configuration
// ============================================================================
#define OTA_URL_MAX_LEN         256
#define OTA_TIMEOUT_MS          30000   // OTA Timeout
#define OTA_HEALTH_CHECK_DELAY_MS 60000  // Health-Check nach OTA (60s)

// ============================================================================
// Power Management Configuration
// ============================================================================
#define POWER_DEEP_SLEEP_ENABLE       true   // Deep Sleep aktivieren
#define POWER_DEEP_SLEEP_TIMEOUT_MS  30000   // Deep Sleep Timeout nach Inaktivität (ms) - 30 Sekunden
#define POWER_DEEP_SLEEP_WAKEUP_GPIO GPIO_PIR_SENSOR  // GPIO für Deep Sleep Wake-Up (PIR-Sensor)
#define POWER_DEEP_SLEEP_WAKEUP_LEVEL 1  // Wake-Up Level (1=HIGH, 0=LOW)
#define WIFI_SLEEP_START_HOUR   22      // WiFi Sleep Start (Uhrzeit)
#define WIFI_SLEEP_END_HOUR     6       // WiFi Sleep Ende (Uhrzeit)
#define DEEP_SLEEP_INACTIVITY_MS 300000 // Deep Sleep nach Inaktivität (5 min) - veraltet
#define BATTERY_CRITICAL_VOLTAGE 3.4    // Kritische Spannung pro Zelle (V)

// ============================================================================
// Stack Monitoring Configuration
// ============================================================================
// Hinweis: FreeRTOS liefert nur den freien Reststack (High-Water-Mark), nicht
// die Gesamtgröße. Daher wird ein absoluter freier Reservepuffer überwacht.
#define STACK_FREE_WARNING_BYTES  768    // Warnung wenn freier Stack < 768 Bytes
#define STACK_FREE_CRITICAL_BYTES 384    // Kritisch wenn freier Stack < 384 Bytes
#define STACK_MONITOR_INTERVAL_MS 10000  // Stack-Überwachung alle 10 Sekunden

// ============================================================================
// Heap Monitoring Configuration
// ============================================================================
#define HEAP_FREE_WARNING_BYTES  50000   // Warnung wenn freier Heap < 50KB
#define HEAP_FREE_CRITICAL_BYTES 20000   // Kritisch wenn freier Heap < 20KB
#define HEAP_MONITOR_INTERVAL_MS  10000  // Heap-Überwachung alle 10 Sekunden

// ============================================================================
// Watchdog Configuration
// ============================================================================
#define TWDT_TIMEOUT_MS         5000    // Watchdog Timeout (5 Sekunden)
#define TWDT_TASK_STACK_SIZE    2048    // Watchdog Task Stack
#define SERVO_EMERGENCY_CLOSE   true    // Servo bei Watchdog-Trigger schließen

// ============================================================================
// Error Logging Configuration
// ============================================================================
#define ERROR_LOG_MAX_ENTRIES   100     // Maximale Fehler-Log-Einträge
#define ERROR_CODE_LENGTH       8       // Länge des Fehlercodes (z.B. "E0012345")

// ============================================================================
// NVS Configuration
// ============================================================================
#define NVS_NAMESPACE "katzenbrunnen"
#define NVS_STORE_NAME "config"
#define NVS_ACTIVATION_COUNT_KEY "activation_cycles"
#define NVS_LAST_TRIGGER_KEY "last_trigger"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_CLOSE_TIMEOUT_MS "close_timeout_ms"
#define NVS_KEY_SERVO_OPEN_US "servo_open_us"
#define NVS_KEY_SERVO_CLOSE_US "servo_close_us"
#define NVS_KEY_FET_ON_TIME_MS "fet_on_time_ms"

// ============================================================================
// Servo Runtime Configuration (NVS-stored)
// ============================================================================
typedef struct {
    uint32_t close_timeout_ms;    // Timeout ohne HIGH-Signal vor Schließen (ms)
    uint32_t servo_open_us;       // Servo-Position offen (µs)
    uint32_t servo_close_us;      // Servo-Position geschlossen (µs)
    uint32_t fet_on_time_ms;      // FET-An-Zeit nach Servo-Bewegung (ms)
} servo_config_t;

// Globale Runtime-Konfiguration
extern servo_config_t g_servo_config;

// Funktionen für Servo-Konfiguration (in main.c implementiert)
esp_err_t save_servo_config_to_nvs(void);

// ============================================================================
// Application Defaults
// ============================================================================
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 1
#define APP_VERSION "0.1.0"
#define APP_NAME "Katzenbrunnen"

// ============================================================================
// Security Configuration (optional)
// ============================================================================
// TLS: Configure your certificates here
// #define USE_TLS_CERTIFICATE 1
// #define TLS_CERT_FILE "certificates/ca-cert.pem"

// ============================================================================
// Logging Configuration
// ============================================================================
// #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

// ============================================================================
// Delay Configuration (Magic Numbers)
// ============================================================================
#define DELAY_1S_MS            1000    // 1 Sekunde Delay
#define DELAY_5S_MS            5000    // 5 Sekunden Delay
#define DELAY_500MS_MS         500     // 500ms Delay
#define DELAY_100MS_MS         100     // 100ms Delay
#define DELAY_50MS_MS          50      // 50ms Delay
#define DELAY_30S_MS           30000   // 30 Sekunden Delay

#endif // CONFIG_H
