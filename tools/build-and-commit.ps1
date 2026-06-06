param(
    [string]$ProjectPath = "$PSScriptRoot\.."
)

$ProjectPath = Resolve-Path -Path $ProjectPath
Set-Location $ProjectPath

# ESP-IDF Umgebung aktivieren (export.bat)
$espIdfPath = "C:\Users\win4g\Downloads\GitHub\VS-Projekte\CascadeProjects\esp-idf"
$exportBat = Join-Path $espIdfPath "export.bat"

if (-not (Test-Path $exportBat)) {
    Write-Host "ESP-IDF export.bat nicht gefunden: $exportBat" -ForegroundColor Red
    exit 1
}

# Build-Nummer inkrementieren
$incrementScript = Join-Path $ProjectPath "tools\increment_build.py"
if (Test-Path $incrementScript) {
    Write-Host "Incrementing build number..." -ForegroundColor Cyan
    & python "$incrementScript"
}

# Build-Nummer auslesen
$buildNumberPath = Join-Path $ProjectPath ".build_number"
$buildNumber = if (Test-Path $buildNumberPath) { Get-Content $buildNumberPath -Raw } else { "?" }

# Build ausführen mit ESP-IDF Umgebung (activate-esp-idf.ps1)
Write-Host "Building project..." -ForegroundColor Cyan
$activateScript = Join-Path $ProjectPath "activate-esp-idf.ps1"
$env:IDF_BUILD_JOBS = "16"
$buildCmd = ". '$activateScript'; idf.py build"
$buildStartTime = Get-Date
powershell -ExecutionPolicy Bypass -NoProfile -Command $buildCmd | Out-Null
$buildExitCode = $LASTEXITCODE
$buildDuration = ((Get-Date) - $buildStartTime).TotalSeconds
$env:IDF_BUILD_JOBS = $null

if ($buildExitCode -ne 0) {
    Write-Host "Build failed! (Dauer: ${buildDuration:F1}s)" -ForegroundColor Red
    exit $buildExitCode
}

Write-Host "Build erfolgreich! (Dauer: ${buildDuration:F1}s)" -ForegroundColor Green

# Alle Änderungen committen (Sourcecode + Build-Metadaten)
Write-Host "Staging all changes..." -ForegroundColor Cyan
& git add -A

$commitMessage = "chore: build #$buildNumber"
& git commit -m $commitMessage 2>&1

if ($LASTEXITCODE -eq 0) {
    Write-Host "All changes committed: $commitMessage" -ForegroundColor Green

    # Push zum Remote Repository
    Write-Host "Pushing to remote repository..." -ForegroundColor Cyan
    $pushResult = & git push 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Push successful" -ForegroundColor Green
    } else {
        Write-Host "Push failed (may need manual push): $pushResult" -ForegroundColor Yellow
    }
} else {
    Write-Host "No changes to commit" -ForegroundColor Yellow
}

# Buildnummer am Schluss ausgeben
Write-Host "Build #$buildNumber completed successfully" -ForegroundColor Green
