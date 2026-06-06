---
description: Vollständige Codeanalyse für ESP32-Projekte
---

# Codeanalyse Workflow für ESP32-Projekte

Dieser Workflow führt eine umfassende Codeanalyse durch, speziell für ESP32/Embedded-Projekte.

## Vorbereitung

1. Prüfen ob Build-Skript existiert
2. Alle relevanten Source-Dateien identifizieren (.c, .h)

## Statische Analyse

### 1. Compiler-Warnings prüfen
- Build durchführen und alle Warnings analysieren
- Besonders auf: unused variables, implicit conversions, missing includes

### 2. Code-Smell-Erkennung
- Duplicate Code suchen (gleiche Code-Blöcke in verschiedenen Dateien)
- Long Functions identifizieren (>100 Zeilen)
- Magic Numbers suchen (harte Zahlen ohne #define)
- Complex Conditions prüfen (mehrere &&/|| verschachtelt)

### 3. Typ-Konsistenz prüfen
- Variablentypen passen zusammen?
- Return-Typen korrekt?
- Casts notwendig und sicher?

## Embedded-Spezifische Analyse

### 4. ISR-Sicherheit prüfen
- ISR-Funktionen mit IRAM_ATTR markiert?
- Keine blocking calls in ISRs?
- 64-Bit Variablen mit portMUX_TYPE geschützt?
- Keine Logging/printf in ISRs?

### 5. Mutex/Synchronisation prüfen
- Alle gemeinsamen Variablen mit Mutex geschützt?
- Keine Deadlocks möglich?
- xSemaphoreTake mit portMAX_DELAY oder Timeout?
- portENTER_CRITICAL/portEXIT_CRITICAL korrekt gepaart?

### 6. GPIO-Konfiguration prüfen
- GPIO-Modus korrekt (INPUT/OUTPUT)?
- Pull-up/Pull-down konfiguriert?
- ISR-Handler korrekt registriert?
- RTC-GPIO für Deep Sleep Wake-Up?

### 7. Watchdog-Management prüfen
- Watchdog vor Sleep gestoppt?
- Watchdog nach Sleep neu gestartet?
- Alle Tasks beim Watchdog angemeldet?
- watchdog_feed() regelmäßig aufgerufen?

### 8. Sleep-Modi prüfen
- Deep Sleep korrekt konfiguriert?
- Light Sleep mit Wake-Up-Quellen?
- Zeit-Synchronisation vor Sleep-Entscheidung?
- Fallback wenn Zeit nicht synchronisiert?

### 9. Resource-Leaks prüfen
- Alle malloc mit free?
- Alle xSemaphoreCreate mit xSemaphoreDelete?
- Alle xTaskCreate mit vTaskDelete?
- Alle Timer gestoppt/gelöscht?

### 10. NVS-Schlüssel-Längen prüfen
- Alle NVS_KEY_* Defines ≤15 Zeichen (ESP-IDF Limit)?
- Schlüssel-Namen eindeutig und beschreibend?
- Namespace korrekt definiert?

## Sicherheitsanalyse

### 11. Buffer Overflows
- strncpy mit korrekter Länge?
- Array-Zugriffe mit Bounds-Checking?
- snprintf statt sprintf?

### 12. Memory Safety
- Null-Pointer Checks?
- Double-free vermieden?
- Use-after-free vermieden?

## Best-Practices Review

### 13. Code-Konsistenz
- Einheitlicher Coding-Style?
- Kommentare in Deutsch (laut global rules)?
- Funktionsnamen beschreibend?

### 14. Architektur
- Separation of Concerns?
- Module sauber getrennt?
- Header-Files nur Deklarationen?

## Bericht erstellen

15. Alle gefundenen Probleme kategorisieren (Kritisch/Mittel/Gering)
16. Priorisierte Liste mit Lösungen erstellen
17. README.md mit Analyse-Ergebnissen aktualisieren (falls nötig)
