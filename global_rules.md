# Globale Regeln

## 1. Projekt-Dokumentation
- Im Projektordner gibt es eine `readme.md` - der Inhalt ist für das Projekt zu beachten
- Die `readme.md` wird regelmäßig aktualisiert und der Code entsprechend korrigiert

## 2. Code-Kommentare
- Kommentare im Code in **Deutsch** hinzufügen
- Kurz und prägnant halten

## 3. FRAGE-AUFGABEN-UNTERSCHEIDUNG (Kritisch)

### 3.1 Reine Fragen (KEINE Aktionen erlaubt)
**Merkmale:** Höflichkeitsformen, Fragezeichen, explorativer Charakter

**Beispiele:**
| Formulierung | Beispiel |
|-------------|----------|
| "könntest du...?" | "könntest du das erklären?" |
| "würdest du...?" | "würdest du das prüfen?" |
| "kannst du...?" | "kannst du mir helfen?" |
| "hättest du...?" | "hättest du eine Idee?" |
| "wäre es möglich...?" | "wäre es möglich das zu ändern?" |
| "wie würde...?" | "wie würde das funktionieren?" |
| Fragezeichen am Ende | "Was bedeutet diese Funktion?" |

**Erlaubt:** Recherche, Analyse, Tests, Erklärungen
**Verboten:** Code-Änderungen, Datei-Modifikationen, Projektstruktur-Änderungen

**Wichtig:** Nicht jedesmal mitteilen, dass eine Frage erkannt wurde.

### 3.2 Aufgaben (Aktionen erlaubt)
**Merkmale:** Direkte Imperative, Aufforderungen, konkrete Handlungsanweisungen

**Beispiele:**
| Formulierung | Aktion |
|-------------|--------|
| "erstelle..." | Neue Datei/Struktur anlegen |
| "lege an..." | Datei/Verzeichnis erstellen |
| "lösche..." | Datei/Verzeichnis entfernen |
| "passe an..." | Existierenden Code ändern |
| "implementiere..." | Funktion/Feature umsetzen |
| "behebe..." | Bug fixen |
| Direkte Imperative | "Sortiere die Liste", "Optimiere das" |

### 3.3 Unsicherheitsregel
**Bei Unsicherheit:** Immer als Frage behandeln und erst um Erlaubnis fragen!

## 4. Arbeitsmethodik

### 4.1 Recherche statt Raten
- **Nicht raten** - bei Ungewissheit recherchieren
- Tools nutzen (file read, code search, grep) statt zu erraten

### 4.2 Code-Validierung
Vor jedem Build/Flash muss Code validiert werden:
1. Anforderung erfüllt?
2. Funktionsfähig? (Logik-Prüfung)
3. Konsistenz (Variablen, NVS-Schlüssel, API, Web-UI)
4. Typ-Prüfung (Datentypen passen zusammen)
5. Default-Werte gesetzt?
6. Best-Practices (Ressourcen-Leaks, Race-Conditions, Error-Handling)
7. Controller-spezifisch (GPIO, I2C, NVS, Watchdog)

## 5. MERKDIR-GLOBAL-REGEL
**Syntax:** "merke dir global [Inhalt]"
**Bedeutung:** Der Wunsch wird in dieser Datei (`global_rules.md`) gespeichert und gilt für alle Projekte.

## 6. Build-Prozess
Bei "build" Kommando immer das **projektspezifische Build-Skript** verwenden (z.B. `build-and-commit.ps1`, `build.sh`, `package.json` scripts), nicht direkt den Compiler/Build-Tool (kein direktes `idf.py build`, `gcc`, `npm run build`, etc.).

**Das Build-Skript sollte automatisch:**
- Build-Metadaten verwalten (Buildnummern, Versionen, Timestamps)
- Bei erfolgreichem Build committen (inkl. Metadaten)
- Änderungen pushen

**Vorteile:**
- Konsistente Build-Prozesse
- Automatische Versionsverwaltung
- Keine vergessenen Commits nach Build

## 7. Webanfragen
- Webanfragen zuerst über Context7 (mcp0_query-docs) statt search_web

## 8. Datei-Codierung (BOM/Encoding)
- **PowerShell-Skripte (.ps1):** UTF-8 ohne BOM oder ASCII verwenden
- **Keine Unicode-Symbole** in Skripten (z.B. ✓, ✗, 🚀, ⚡, ✅, ❌, ⚠️)
- **Alternativen:** [OK], [X], [INIT], [FAST], [ERROR], [WARN]
- **Umlaute:** Für maximale Kompatibilität ae/oe/ue verwenden (optional)
- **Grund:** PowerShell interpretiert Unicode-Symbole als Array-Index-Ausdrücke und wirft Parser-Fehler
