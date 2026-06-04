# OTA-Sicherheit: Wie man Bricks vermeidet

## Problem: OTA kann ESP32 unbrauchbar machen

Bei Fehlern während OTA-Updates kann der ESP32 in einem Boot-Loop feststecken oder komplett nicht mehr erreichbar sein.

## Lösungen für sichere OTA-Updates

### 1. **A/B-Partitionierung (bereits implementiert)**
- ESP32 hat 2 OTA-Partitionen (ota_0, ota_1)
- Bei Fehler bootet von letzter funktionierender Partition
- **Status**: Aktiv durch `partitions.csv`

### 2. **Pre-OTA-Validierung (TODO)**
```c
// Prüft vor OTA ob Partitionen vorhanden
const esp_partition_t *running = esp_ota_get_running_partition();
const esp_partition_t *ota = esp_ota_get_next_update_partition(NULL);
```

### 3. **Firmware-Größen-Prüfung (TODO)**
```c
// Prüfen ob neue Firmware in Partition passt
if (new_firmware_size > partition_size) {
    return ERROR;
}
```

### 4. **Rollback-Mechanismus (TODO)**
```c
// Bei Boot-Fehler automatisch zurückrollen
esp_ota_mark_app_invalid_rollback_and_reboot();
```

### 5. **Watchdog mit Reset (TODO)**
```c
// Wenn System nach OTA nicht antwortet -> Hard Reset
if (no_response_for_60_seconds) {
    esp_restart();
}
```

## Aktuelle Sicherheitseinstellungen

### Partitionierung (16MB):
- **Bootloader**: 0x1000 (4KB)
- **Partition Table**: 0x8000 (32KB) 
- **ota_0**: 0x20000-0x320000 (3MB)
- **ota_1**: 0x320000-0x620000 (3MB)
- **Storage**: 0x620000-0x1000000 (10MB)
- **NVS**: 0x9000-0xF000 (24KB)
- **OTA Data**: 0xF000-0x11000 (8KB)
- **Phy Init**: 0x11000-0x12000 (4KB)

### Stack-Sizes (aktuell, siehe config.h):
- App: 8192 bytes
- Control: 4096 bytes
- Web Server: 16384 bytes
- WiFi: 8192 bytes
- OTA: 8192 bytes

## Best Practices für OTA

### Vor OTA:
1. **Backup erstellen**: Firmware-Binary sichern
2. **Version prüfen**: Neue Firmware sollte stabil sein
3. **Speicher prüfen**: Genug RAM/Flash verfügbar?
4. **Netzwerk stabil**: WLAN-Verbindung muss zuverlässig sein

### Während OTA:
1. **Status überwachen**: `/api/ota/status` alle 2 Sekunden
2. **Timeout beachten**: Bei 30 Sekunden Stillstand abbrechen
3. **Logs prüfen**: Fehlermeldungen beachten

### Nach OTA:
1. **Boot testen**: System sollte innerhalb von 60 Sekunden antworten
2. **Funktionen prüfen**: PIR, Servo, Web-UI
3. **Bei Problemen**: Sofort Rollback durchführen

## Notfall-Plan

### Wenn ESP32 nicht erreichbar:
1. **Warten**: Bis zu 2 Minuten (Boot kann dauern)
2. **Power-Cycle**: Strom aus/ein für 10 Sekunden
3. **USB-Flash**: `idf.py -p PORT flash` mit letzter funktionierender Firmware
4. **Factory Reset**: Partition Table neu flashen

### USB-Flash Befehle:
```bash
# Letzte funktionierende Firmware flashen
idf.py -p COM3 flash

# Kompletter Reset (letzter Ausweg)
idf.py -p COM3 erase-flash
idf.py -p COM3 flash
```

## Monitoring

### OTA-Status API:
```json
GET /api/ota/status
{
  "status": "OK",
  "ota": {
    "in_progress": false,
    "last_result_ok": true,
    "phase": "SUCCESS",
    "message": "OTA erfolgreich, Neustart folgt"
  }
}
```

### System-Status API:
```json
GET /api/status
{
  "status": "OK",
  "system": {
    "app_version": "v0.1.22 (2026-06-02)",
    "uptime_ms": 12345,
    "free_heap_bytes": 192000
  }
}
```

## OTA-Server

### Firmware hochladen:
```powershell
# Firmware auf OTA-Server hochladen
.\ota_upload.ps1
```

### OTA starten:
```powershell
# OTA auf ESP32 starten
.\ota.ps1
```

### OTA-Request-Template:
```json
{
  "url": "http://192.168.1.191:8070/katzenbrunnen.bin"
}
```

## Zukünftige Verbesserungen

1. **Automatische Firmware-Validierung**
   - SHA-256 Hash-Prüfung
   - Versions-Kompatibilitäts-Check

2. **Intelligentes Rollback**
   - Automatische Erkennung von Boot-Problemen
   - Selbstheilung ohne Eingriff

3. **OTA mit Delta-Updates**
   - Kleinere Downloads
   - Geringere Fehleranfälligkeit

4. **OTA-Queue**
   - Updates nur bei stabilem System
   - Automatische Retry bei Fehlern

---

**Wichtig**: Diese Sicherheitseinstellungen reduzieren das Risiko von OTA-Bricks erheblich, aber eliminieren es nicht vollständig. Immer ein USB-Flash-Backup bereithalten!

**Hinweis**: OTA ist in ESP-IDF 6.1 aktuell deaktiviert (API-Änderungen). Die Struktur ist vorbereitet für zukünftige Implementierung.
