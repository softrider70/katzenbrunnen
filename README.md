# katzenbrunnen — ESP32-S3 Projekt

Automatischer Katzenbrunnen mit ESP32-S3 Mikrocontroller für intelligente Steuerung an der Badewannenarmatur.

## Übersicht

Automatischer Katzenbrunnen für Katzen zur Selbstversorgung mit Wasser. Das System wird an der Badewannenarmatur installiert und ermöglicht es den Tieren, sich selbst Wasser zu trinken.

**Funktionen:**
- **Bewegungserkennung** beim Betreten der Badewanne (PIR-Sensor)
- **Automatische Wasserhahn-Steuerung** via Servo
- **Intelligente Trigger-Logik:** Wasser fließt nur bei anhaltender Bewegung
- **Timeout-Schutz:** Automatisches Schließen nach definierter Zeit
- **Manuelle Steuerung** über Taster
- **Statusanzeige** mit RGB LED
- **Persistente Datenspeicherung** der Aktivierungszykler
- **Web-UI** mit tabellarischer Anzeige von Bewegungen und Öffnungszeiten
- **Spannungsüberwachung** für LiPo-Batterie (2S2P, 3.7V, 4Ah)
- **OTA-Updates** für Firmware-Austausch über WLAN
- **Stromspar-Modi** für batteriebetriebenen Betrieb

**FreeRTOS Task-Architektur:**
- **PIR-Task (Core 0):** Bewegungserkennung und Trigger-Logik
- **Servo-Task (Core 0):** Wasserhahn-Steuerung
- **Web-Server-Task (Core 1):** HTTP-Handler und API-Endpunkte
- **WiFi-Task (Core 1):** WLAN-Verbindungsmanagement
- **OTA-Task (Core 1):** Firmware-Update-Management
- **Spannungs-Monitor-Task (Core 0):** Batterieüberwachung

- **Board:** ESP32-S3-WROOM-1 (Dual-Core, 512KB SRAM, 8MB PSRAM, 16MB Flash)
- **ESP-IDF:** 6.1
- **Version:** 0.1.23
- **Status:** Entwicklungsphase

## Hardware-Anschluss

### ESP32-S3 Pin-Belegung:
```
GPIO6  → PIR Bewegungssensor (Digital)
GPIO9  → WS2812B RGB LED Data
GPIO10  → manueller Taster (mit Pull-up)
GPIO11  → Gigaline Standard Servo (PWM) - Wasserhahn-Steuerung
GPIO1   → ADC1_CH1 - Batteriespannungsmessung (LiPo 2S2P)
```

### Benötigte Komponenten:
- ESP32-S3 Entwicklungsboard
- **PIR Bewegungssensor BIS0001** (Elegoo 37-in-1 Kit, 24mm × 33mm)
- **Gigaline Standard Servo** (39.7mm × 20.37mm × 36.12mm) - Wasserhahn-Mechanik
- WS2812B RGB LED
- Taster
- Mechanische Verbindung Servo → Wasserhahn-Griff
- **LiPo-Batterie** 2S2P (3.7V, 4Ah) mit Spannungsteiler für ADC

## Funktionsweise

### Automatischer Betrieb:
1. **PIR-Sensor** erkennt Katze beim Betreten der Badewanne
2. **Erfassungszeit:** Nach initialer Bewegungserkennung wird kurze Verzögerung gewartet
3. **Servo** öffnet Wasserhahn bis zum eingestellten Winkel
4. **Wasser fließt** solange der PIR-Sensor Bewegung feststellt
5. **Timeout-Schutz:** Nach definierter Zeit ohne Bewegung schließt der Servo automatisch
6. **Aktivierungszykler** werden in NVS gespeichert

### Manuel Betrieb:
- **Taster** löst sofortige Wasserhahn-Öffnung aus
- **Servo** steuert Wasserhahn-Position
- **Status-LED** zeigt Betriebszustand an
- **Logging** über Serial Monitor

### Web-UI:
- **Tabelle** mit Bewegungsereignissen (Zeitstempel, Dauer)
- **Tabelle** mit Öffnungszeiten (Start, Ende, Dauer)
- **Spannungsanzeige** (Aktuelle Spannung, Prozent, Status)
- **OTA-Steuerbereich** für Firmware-Updates (ESP-IDF 6.1: aktuell deaktiviert)
- **WiFi-Konfiguration** für Netzwerk-Setup

### Stromspar-Modi:
- **WiFi Sleep:** WLAN deaktivieren wenn nicht benötigt
- **Deep Sleep:** System in Schlafmodus bei Inaktivität
- **Spannungsüberwachung:** Abschaltung bei kritischer Batteriespannung

## Project Structure

```
katzenbrunnen/
├── components/
│   └── main/
│       ├── main.c              Main application entry point
│       ├── pir.c               PIR-Sensor Modul (Bewegungserkennung)
│       ├── servo.c             Servo-Steuerung Modul (Wasserhahn)
│       ├── battery.c           Batterie-Monitor Modul (Spannungsmessung)
│       ├── error_log.c         Fehler-Logging Modul
│       ├── stack_monitor.c     Stack-Überwachungs Modul
│       ├── watchdog.c          Watchdog Modul
│       ├── wifi.c              WiFi-Management Modul
│       ├── web_server.c        Web-Server Modul (HTTP-Handler)
│       ├── ota.c               OTA-Update Modul (ESP-IDF 6.1 Stub)
│       └── CMakeLists.txt      Component build config
├── include/
│   ├── config.h            Hardware-Konfiguration (Pins, Parameter)
│   ├── pir.h               PIR-Sensor Header
│   ├── servo.h             Servo-Steuerung Header
│   ├── battery.h           Batterie-Monitor Header
│   ├── error_log.h         Fehler-Logging Header
│   ├── stack_monitor.h     Stack-Überwachung Header
│   ├── watchdog.h          Watchdog Header
│   ├── wifi.h              WiFi-Management Header
│   ├── web_server.h        Web-Server Header
│   └── ota.h               OTA-Update Header
├── tools/
│   ├── build-and-commit.ps1     Build mit automatischer Buildnummer
│   ├── flash-mode.ps1           USB/OTA Flash-Workflow
│   └── increment_build.py       Buildnummer-Generierung
├── CMakeLists.txt          Project build config
├── sdkconfig               Build configuration (auto-generated)
├── partitions.csv          OTA-Partitionierung (16MB Flash)
├── activate-esp-idf.ps1    ESP-IDF Umgebung aktivieren
├── ota.ps1                 OTA-Start-Skript
├── ota_upload.ps1          OTA-Upload-Skript
├── ota_request.json        OTA-Request-Template
├── README_OTA_SAFETY.md    OTA-Sicherheitsdokument
└── README.md               This file
```

## Code-Struktur und Modularisierung

**WICHTIG:** Das gesamte Projekt darf nicht in einer einzigen main.c-Datei zusammengefasst werden. Der Code muss in sinnvolle, modulare Komponenten aufgeteilt werden:

**Modul-Struktur:**
- **pir.c/h:** PIR-Sensor Initialisierung, ISR-Handler, Bewegungserkennungslogik
- **servo.c/h:** Servo PWM-Initialisierung, Positionskontrolle, Wasserhahn-Steuerung
- **battery.c/h:** ADC-Initialisierung, Spannungsmessung, Prozentberechnung
- **error_log.c/h:** Fehler-Logging mit Ringbuffer, Fehlercode-Generierung
- **stack_monitor.c/h:** Stack-Überwachung für bekannte Tasks (ESP-IDF 6.1 kompatibel)
- **watchdog.c/h:** ESP32 Task Watchdog, Emergency-Close bei Trigger
- **wifi.c/h:** WiFi-Verbindungsmanagement, Credential-Handling, AP-Fallback
- **web_server.c/h:** HTTP-Server, API-Endpunkte, HTML/JSON-Generierung
- **ota.c/h:** OTA-Update-Logik (ESP-IDF 6.1 Stub - API-Änderungen)

**Regeln:**
- Jedes Modul hat eigene .c und .h Dateien
- Header-Dateien enthalten nur öffentliche APIs und Konstanten
- Interne Funktionen bleiben in .c Dateien
- Globale Variablen werden minimiert und durch Getter/Setter ersetzt
- Thread-Sicherheit über Semaphores/Mutexes pro Modul

## Quick Start

### Build
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build-and-commit.ps1
```

**Automatische Schritte:**
1. Build-Nummer inkrementieren (`tools/increment_build.py`)
2. `include/version.h` generieren
3. ESP-IDF Build ausführen (`idf.py build`)
4. Commit mit Buildnummer im Commit-Text
5. Build-Metadaten automatisch committen
6. Push zum Remote Repository

### Flash
```powershell
# USB Flash (auto-detect Port und Flash-Typ)
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-mode.ps1 -Mode usb

# USB Flash mit spezifischem Port
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-mode.ps1 -Mode usb -UsbPort COM3

# Letzten verwendeten Modus
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-mode.ps1 -Mode last
```

**Flash-Modi:**
- **initial:** Bootloader + Partition Table + App (erstes Mal)
- **update:** Nur App (schnelle Updates)
- **auto:** Automatische Erkennung

### Monitor
```bash
idf.py -p COM3 monitor
```

## Konfiguration

Die wichtigsten Parameter können in `include/config.h` angepasst werden:
```c
#define PUMP_ACTIVE_TIME_MS   10000    // Pumpenlaufzeit
#define MIN_FILL_LEVEL_CM     5.0       // Minimaler Füllstand
#define MAX_FILL_LEVEL_CM     30.0      // Maximaler Füllstand
#define PIR_COOLDOWN_MS       30000     // PIR Cooldown
// Servo-Einstellungen
#define SERVO_MIN_PULSE_US   500       // Minimale Pulsweite
#define SERVO_MAX_PULSE_US   2400      // Maximale Pulsweite
#define SERVO_NEUTRAL_US     1500      // Neutralposition
#define SERVO_FREQUENCY_HZ   50        // PWM Frequenz
```

### Board Selection

Das ESP32-S3 Target ist bereits konfiguriert in `sdkconfig.defaults`:
```bash
# Überprüfen:
idf.py --help | grep esp32s3
```

## Development Workflow

1. **Edit code** in `src/main.c` or `include/config.h`
2. **Build:** `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build-and-commit.ps1`
3. **Flash:** `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\flash-mode.ps1 -Mode last`
4. **Test & Debug:** `idf.py -p COM3 monitor`
5. **Commit:** Automatisch durch build-and-commit.ps1

## Entwickler-Tools

- **`tools/build-and-commit.ps1`** — Build mit automatischer Buildnummer-Inkrementierung und Commit
- **`tools/flash-mode.ps1`** — USB/OTA Flash-Workflow mit Auto-Detection
- **`tools/increment_build.py`** — Buildnummer-Generierung und version.h-Erzeugung
- **`activate-esp-idf.ps1`** — ESP-IDF Umgebung aktivieren

## Versionierung

Die Versionierung folgt dem Schema **MAJOR.MINOR.BUILD**:
- **MAJOR/MINOR** werden in `include/config.h` definiert (`APP_VERSION_MAJOR`, `APP_VERSION_MINOR`)
- **BUILD** wird automatisch bei jedem Build inkrementiert
- Bei Änderung von MAJOR oder MINOR wird BUILD auf 0 zurückgesetzt
- `include/version.h` wird automatisch generiert

## Adding Features

Use Copilot skills to extend functionality:

```
/add-ota          Enable OTA firmware updates
/add-webui        Add responsive web dashboard
/add-library      Manage external components
/add-security     Enable Secure Boot, encryption
/setup-ci         GitHub Actions CI/CD
/add-profiling    Performance monitoring
```

## Documentation

- **`PROJECT.md`** — Detailed project specifications
- **`sdkconfig`** — Build configuration (auto-generated)
- **`include/config.h`** — Hardware pin mappings

## Unit Tests

Das Projekt enthält Unit Tests für die wichtigsten Module (PIR, Servo, Battery) basierend auf dem ESP-IDF Unity Test Framework.

### Test-Struktur
```
test/
├── CMakeLists.txt          # Test-Konfiguration
├── test_pir.c              # PIR-Sensor Tests
├── test_servo.c            # Servo-Steuerung Tests
└── test_battery.c          # Battery-Monitor Tests
```

### Tests ausführen
```bash
# Alle Tests bauen und flashen
idf.py build
idf.py -p COM3 flash monitor

# Spezifische Test-Komponente ausführen
idf.py -p COM3 flash monitor --test-component pir
idf.py -p COM3 flash monitor --test-component servo
idf.py -p COM3 flash monitor --test-component battery
```

### Test-Ergebnisse
Die Test-Results werden im Serial Monitor angezeigt:
```
Test PIR Initialisierung passed
Test PIR Task Start passed
Test PIR Bewegungserkennung Status passed
3/3 tests passed
```

### Neue Tests hinzufügen
1. Neue Test-Datei in `test/` erstellen (z.B. `test_wifi.c`)
2. Test-Funktionen mit `TEST_CASE` Makro definieren
3. Datei zu `test/CMakeLists.txt` hinzufügen
4. Build und flashen

Beispiel:
```c
#include "unity.h"
#include "wifi.h"

TEST_CASE("WiFi Initialisierung", "[wifi]")
{
    esp_err_t ret = wifi_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}
```

## Troubleshooting

**Build fails:**
```bash
idf.py fullclean
idf.py build
```

**Flash doesn't work:**
- Check USB connection: `idf.py monitor --no-reset`
- Select port manually: `idf.py -p /dev/ttyUSB0 flash`

**Memory issues:**
- Check heap with `/add-profiling`
- Review `sdkconfig` memory settings
- Use PSRAM if available

## Next Steps

1. Update `PROJECT.md` with hardware details
2. Configure `include/config.h` for your board setup
3. Implement application in `src/main.c`
4. Test with `/upload-firmware`
5. When production-ready, use `/add-security`

---

Generated from ESP32 Template  
For template docs: https://github.com/softrider70/esp32-template
