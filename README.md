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
- **OTA-Updates** für Firmware-Austausch über WLAN

**FreeRTOS Task-Architektur:**
- **PIR-Task (Core 0):** Bewegungserkennung und Trigger-Logik
- **Servo-Task (Core 0):** Wasserhahn-Steuerung
- **Web-Server-Task (Core 1):** HTTP-Handler und API-Endpunkte
- **WiFi-Task (Core 1):** WLAN-Verbindungsmanagement
- **OTA-Task (Core 1):** Firmware-Update-Management

- **Board:** ESP32-S3-WROOM-1 (Dual-Core, 512KB SRAM, 8MB PSRAM, 16MB Flash)
- **ESP-IDF:** 6.1
- **Version:** 0.1.23
- **Status:** Entwicklungsphase

## Hardware-Anschluss

### ESP32-S3 Pin-Belegung:
```
GPIO4  → PIR Bewegungssensor (Digital)
GPIO48 → WS2812B RGB LED Data (fest verdrahtet vom Hersteller)
GPIO10  → manueller Taster (mit Pull-up)
GPIO11  → Gigaline Standard Servo (PWM) - Wasserhahn-Steuerung
GPIO5   → 2N7000 MOSFET Gate (Low-Side-Switching für Servo-Stromversorgung)
```

### Servo-Stromversorgung (2N7000 Low-Side-Switching):
- **2N7000 N-Channel MOSFET** für Servo-Stromversorgung (~190mA)
- **Schaltung:** Source → GND, Drain → Servo-GND, Gate → GPIO5
- **Funktion:** GPIO5 HIGH schaltet Servo ein, GPIO5 LOW schaltet Servo aus
- **Vorteil:** Servo wird komplett stromlos geschaltet, keine Standby-Verluste
- **FET-Stromspar-Logik:** Bei jeder Servo-Bewegung: FET an → Servo positionieren → Timeout (5s) → FET aus
- **Servo-Initialisierung:** Beim Boot nur FET stromlos schalten, keine Servo-Bewegung
- **Kalibrierungsfahrt:** Nach Zeitsynchronisierung: geschlossen → 1s → 100µs Richtung offen → 1s → geschlossen → FET aus
- **Servo-Haltefunktion:** Servo hält Position ohne Strom (keine Nachregelung nötig)

### Benötigte Komponenten:
- ESP32-S3 Entwicklungsboard
- **PIR Bewegungssensor BIS0001** (Elegoo 37-in-1 Kit, 24mm × 33mm)
- **Gigaline Standard Servo** (39.7mm × 20.37mm × 36.12mm) - Wasserhahn-Mechanik
- **2N7000 N-Channel MOSFET** (TO-92) - Servo-Stromversorgung (Low-Side-Switching)
- WS2812B RGB LED
- Taster
- Mechanische Verbindung Servo → Wasserhahn-Griff
- **Netzteil** 5V DC (min. 2A)

## Funktionsweise

### Automatischer Betrieb:
1. **PIR-Sensor** erkennt Katze beim Betreten der Badewanne (pulsierendes Signal: 2.5s HIGH → 5s LOW → 2.5s HIGH)
2. **PIR-Logik für pulsierendes Signal:**
   - HIGH-Signal zurücksetzt "Objekt weg"-Timer
   - LOW-Signal prüft ob 10s ohne HIGH → Objekt weg, Timer zurücksetzen
   - Öffnen erst nach 10s HIGH-Signale (MIN_MOTION_DURATION_MS)
   - Objekt gilt als weg wenn 10s durchgehend kein HIGH (PIR_MOTION_TIMEOUT_MS)
3. **Servo** öffnet Wasserhahn bis zum eingestellten Winkel
4. **Wasser fließt** solange der PIR-Sensor Bewegung feststellt
5. **Timeout-Schutz:** Nach 8s (einstellbar, 1-30s) ohne HIGH-Signal schließt der Servo automatisch (CLOSE_TIMEOUT_MS)
6. **Cooldown:** Nach Schließen 30s Cooldown vor erneutem Öffnen (PIR_COOLDOWN_MS)
7. **Aktivierungszykler** werden in NVS gespeichert

### Manuel Betrieb:
- **Taster** löst sofortige Wasserhahn-Öffnung aus
- **Servo** steuert Wasserhahn-Position
- **Status-LED** zeigt Betriebszustand an
- **Logging** über Serial Monitor

### Web-UI:
- **Tabelle** mit Bewegungsereignissen (Zeitstempel, Dauer)
- **Tabelle** mit Öffnungszeiten (Start, Ende, Dauer)
- **OTA-Steuerbereich** für Firmware-Updates (ESP-IDF 6.1: aktuell deaktiviert)
- **WiFi-Konfiguration** für Netzwerk-Setup
- **Servo-Konfiguration** (neu):
  - Close-Timeout (1-30 Sekunden) - Zeit ohne HIGH-Signal vor Schließen (Default: 8s)
  - Servo-Position offen (100-1000µs) - Pulsweite für geöffneten Wasserhahn (Default: 120µs)
  - Servo-Position geschlossen (100-1000µs) - Pulsweite für geschlossenen Wasserhahn (Default: 750µs)
  - FET-An-Zeit (1-10 Sekunden) - Zeit für Servo-Stellzeit vor Stromabschaltung
- **Error-Log-Anzeige** mit farbcodierter Schweregrad-Indikator

### WiFi-Einrichtung (Captive Portal):
- **AP-Start bei fehlenden Credentials:** Wenn kein WiFi-Passwort im NVS gespeichert ist, startet automatisch der Access Point
- **AP-Start nach 3 Fehlversuchen:** Wenn 3 aufeinanderfolgende Anmeldeversuche mit gespeicherten Credentials fehlschlagen, startet der AP
- **Captive Portal:** Nach Verbindung mit dem AP wird der Client automatisch auf die WiFi-Einrichtungsseite weitergeleitet
- **Einrichtung über Web-UI:** SSID und Passwort können direkt im Browser eingegeben werden
- **Automatische Verbindung:** Nach erfolgreicher Einrichtung verbindet sich das Gerät automatisch mit dem konfigurierten Netzwerk
- **mDNS-Hostname:** Gerät ist unter `katzenbrunnen.local` im Netzwerk erreichbar

### System-Reset Funktionen:
- **WiFi-Reset:** Löscht gespeicherte WiFi-Credentials und startet AP-Modus neu
- **System-Reset:** Neustart des gesamten ESP32-Systems


## Project Structure

```
katzenbrunnen/
├── components/
│   └── main/
│       ├── main.c              Main application entry point
│       ├── pir.c               PIR-Sensor Modul (Bewegungserkennung)
│       ├── servo.c             Servo-Steuerung Modul (Wasserhahn)
│       ├── error_log.c         Fehler-Logging Modul
│       ├── stack_monitor.c     Stack-Überwachungs Modul
│       ├── heap_monitor.c      Heap-Überwachungs Modul
│       ├── watchdog.c          Watchdog Modul
│       ├── wifi.c              WiFi-Management Modul
│       ├── web_server.c        Web-Server Modul (HTTP-Handler)
│       ├── ota.c               OTA-Update Modul (ESP-IDF 6.1 Stub)
│       └── CMakeLists.txt      Component build config
├── include/
│   ├── config.h            Hardware-Konfiguration (Pins, Parameter)
│   ├── pir.h               PIR-Sensor Header
│   ├── servo.h             Servo-Steuerung Header
│   ├── error_log.h         Fehler-Logging Header
│   ├── stack_monitor.h     Stack-Überwachung Header
│   ├── heap_monitor.h      Heap-Überwachung Header
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
- **error_log.c/h:** Fehler-Logging mit Ringbuffer, Fehlercode-Generierung
- **stack_monitor.c/h:** Stack-Überwachung für bekannte Tasks (ESP-IDF 6.1 kompatibel)
- **heap_monitor.c/h:** Heap-Überwachung mit Warnung/Kritisch-Schwellen
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
// PIR-Sensor Einstellungen
#define PIR_COOLDOWN_MS       30000     // PIR Cooldown nach Schließen
#define MIN_MOTION_DURATION_MS 10000    // Minimale Bewegungsdauer für Aktivierung (pulsierendes Signal)
#define PIR_MOTION_TIMEOUT_MS 10000    // Objekt weg wenn kein HIGH für 10s
#define MOTION_TIMEOUT_MS     60000     // Timeout ohne Bewegung vor Schließen

// Servo-Einstellungen
#define SERVO_OPEN_ANGLE_US   170       // Servo-Position für geöffneten Wasserhahn (Pulsweite in µs)
#define SERVO_CLOSE_ANGLE_US  780       // Servo-Position für geschlossenen Wasserhahn (Pulsweite in µs)
#define SERVO_FREQUENCY_HZ    50        // PWM Frequenz

// HTTPD Konfiguration
#define HTTPD_MAX_URI_HANDLERS 32       // Maximale Anzahl HTTP-Handler (erhöht für Web-UI)
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

Das Projekt enthält Unit Tests für die wichtigsten Module (PIR, Servo) basierend auf dem ESP-IDF Unity Test Framework.

### Test-Struktur
```
test/
├── CMakeLists.txt          # Test-Konfiguration
├── test_pir.c              # PIR-Sensor Tests
└── test_servo.c            # Servo-Steuerung Tests
```

### Tests ausführen
```bash
# Alle Tests bauen und flashen
idf.py build
idf.py -p COM3 flash monitor

# Spezifische Test-Komponente ausführen
idf.py -p COM3 flash monitor --test-component pir
idf.py -p COM3 flash monitor --test-component servo
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

## Resource Management

### Mutex Cleanup
Alle Module mit Mutex-Synchronisation haben entsprechende `*_deinit()` Funktionen:
- `wifi_module_deinit()` - WiFi-Modul (umbenannt wegen Namenskonflikt mit esp_wifi-Library)
- `stack_monitor_deinit()` - Stack-Monitor
- `heap_monitor_deinit()` - Heap-Monitor
- `servo_deinit()` - Servo-Modul
- `ota_deinit()` - OTA-Modul
- `error_log_deinit()` - Error-Log
- `deinit_hardware()` - Hardware-State Mutex

Diese Funktionen löschen die Mutexes und verhindern Memory Leaks. Für Embedded-Systeme werden diese Funktionen typischerweise nur bei System-Reset oder Shutdown aufgerufen.

### Task Lifecycle
Die folgenden FreeRTOS-Tasks laufen bis zum System-Reset:
- `control_task` - Hauptsteuerung (PIR, Servo)
- `wifi_task` - WiFi-Management
- `stack_monitor_task` - Stack-Überwachung
- `heap_monitor_task` - Heap-Überwachung
- `dns_server_task` - DNS-Server

Tasks werden nicht explizit gelöscht (`vTaskDelete`), da sie für den gesamten Betrieb des Systems benötigt werden. Dies ist für Embedded-Systeme akzeptabel und üblich.

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
