# Prueba de humo del servidor con curl.exe.
# Requisito: el servidor corriendo, p. ej.:
#   server\build\Release\scanner_server.exe --port 8080 --data-dir sessions
# Uso: powershell -ExecutionPolicy Bypass -File scripts\smoke-test.ps1 [-BaseUrl http://127.0.0.1:8080] [-DataDir sessions]
param(
    [string]$BaseUrl = "http://127.0.0.1:8080",
    [string]$DataDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "sessions")
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$images = Join-Path $repo "server\tests\data"
$failures = 0

function Check([string]$what, [bool]$ok, [string]$detail = "") {
    if ($ok) { Write-Host "[OK]    $what" -ForegroundColor Green }
    else { Write-Host "[FALLA] $what $detail" -ForegroundColor Red; $script:failures++ }
}

function Invoke-Curl([string[]]$curlArgs) {
    $out = & curl.exe -s -w "`n%{http_code}" @curlArgs
    $lines = ($out -join "`n") -split "`n"
    return [pscustomobject]@{ Status = [int]$lines[-1]; Body = ($lines[0..($lines.Count - 2)] -join "`n") }
}

# 1. /health
$r = Invoke-Curl @("$BaseUrl/health")
Check "GET /health responde 200" ($r.Status -eq 200) $r.Body
if ($r.Status -ne 200) { Write-Host "¿Está corriendo el servidor en $BaseUrl?"; exit 1 }

# 2. Crear sesión (el JSON va en un archivo para evitar problemas de comillas en PowerShell 5.1)
$tmp = New-Item -ItemType Directory -Force (Join-Path $env:TEMP "escaner3d-smoke")
$bodyFile = Join-Path $tmp "session.json"
'{"name":"humo","device":{"model":"smoke-test","android":"-"}}' | Set-Content -Encoding ascii $bodyFile
$r = Invoke-Curl @("-X", "POST", "-H", "Content-Type: application/json", "--data-binary", "@$bodyFile", "$BaseUrl/sessions")
Check "POST /sessions responde 201" ($r.Status -eq 201) $r.Body
$id = ($r.Body | ConvertFrom-Json).session_id
Write-Host "        session_id = $id"

# 3. Subir 2 fotos: una nítida y una borrosa
$photos = @(@{ Index = 1; File = "nitida.jpg"; Accepted = $true }, @{ Index = 2; File = "borrosa.jpg"; Accepted = $false })
foreach ($p in $photos) {
    $metaFile = Join-Path $tmp "meta$($p.Index).json"
    ('{"index":' + $p.Index + ',"captured_at":"2026-11-10T15:31:02-04:00","width":640,"height":480,' +
     '"focal_mm":null,"focal_35mm":null,"orientation_q":[1,0,0,0]}') | Set-Content -Encoding ascii $metaFile
    $img = Join-Path $images $p.File
    $r = Invoke-Curl @("-X", "POST", "-F", "image=@$img;type=image/jpeg", "-F", "metadata=<$metaFile;type=application/json",
                       "$BaseUrl/sessions/$id/photos")
    $v = $r.Body | ConvertFrom-Json
    Check "POST foto $($p.Index) ($($p.File)) responde 200" ($r.Status -eq 200) $r.Body
    Check "foto $($p.Index): accepted = $($p.Accepted) (nitidez $([math]::Round($v.sharpness,1)))" ($v.accepted -eq $p.Accepted) $r.Body
}

# 4. Errores esperados
$r = Invoke-Curl @("-X", "POST", "$BaseUrl/sessions/no-existe/complete")
Check "sesión inexistente responde 404" ($r.Status -eq 404) $r.Body

# 5. Completar
$r = Invoke-Curl @("-X", "POST", "$BaseUrl/sessions/$id/complete")
Check "POST /complete responde 200" ($r.Status -eq 200) $r.Body
Write-Host "        resumen = $($r.Body)"

# 6. Archivos en disco
$sessionDir = Join-Path $DataDir $id
Check "existe la carpeta $sessionDir" (Test-Path $sessionDir)
Check "existen img_001.jpg e img_002.jpg" ((Test-Path "$sessionDir\img_001.jpg") -and (Test-Path "$sessionDir\img_002.jpg"))
Check "existe manifest.json" (Test-Path "$sessionDir\manifest.json")
if (Test-Path "$sessionDir\manifest.json") {
    $m = Get-Content -Raw "$sessionDir\manifest.json" | ConvertFrom-Json
    Check "el manifiesto tiene 2 fotos y rechaza la 2" (($m.photos.Count -eq 2) -and ($m.summary.rejected -contains 2))
}

Remove-Item -Recurse -Force $tmp
if ($failures -eq 0) { Write-Host "`nPrueba de humo correcta." -ForegroundColor Green; exit 0 }
Write-Host "`n$failures verificación(es) fallaron." -ForegroundColor Red
exit 1
