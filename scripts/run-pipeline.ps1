<#
.SYNOPSIS
  Ejecuta el pipeline completo del Escáner 3D sobre una sesión ya recibida por el servidor:
  scanner_match -> scanner_reconstruct -> scanner_dense -> scanner_texture -> scanner_accuracy.

.DESCRIPTION
  Muestra el tiempo de cada etapa y se detiene con un mensaje claro si una falla.
  Las herramientas se buscan en server\build\Release (compila antes el servidor).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run-pipeline.ps1 -Session sessions\20261121-142259-tenis

.EXAMPLE
  # Con escala: dos puntos del modelo (MeshLab) y su distancia real en mm.
  powershell -ExecutionPolicy Bypass -File scripts\run-pipeline.ps1 -Session sessions\<id> -ScaleRef "0.1,0.2,3.1;0.4,0.2,3.0;85"
#>
param(
  [Parameter(Mandatory = $true)][string]$Session,
  [ValidateSet('ours', 'colmap')][string]$Poses = 'ours',
  [string]$ScaleRef = '',
  [double]$Scale = 0,
  [string]$Colmap = '',
  [switch]$SkipMatch,
  [switch]$SkipAccuracy
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'server\build\Release'
if (-not (Test-Path $Session)) { Write-Host "No existe la sesión $Session" -ForegroundColor Red; exit 2 }
$Session = (Resolve-Path $Session).Path
foreach ($tool in 'scanner_match', 'scanner_reconstruct', 'scanner_dense', 'scanner_texture', 'scanner_accuracy') {
  if (-not (Test-Path (Join-Path $bin "$tool.exe"))) {
    Write-Host "Falta $tool.exe en $bin. Compila el servidor (README, sección Servidor)." -ForegroundColor Red
    exit 2
  }
}

$colmapArgs = @()
if ($Colmap -ne '') { $colmapArgs = @('--colmap', $Colmap) }
$scaleArgs = @()
if ($ScaleRef -ne '') { $scaleArgs = @('--scale-ref', $ScaleRef) } elseif ($Scale -gt 0) { $scaleArgs = @('--scale', "$Scale") }

$stages = @()
if (-not $SkipMatch) {
  $stages += @{ Name = 'Emparejamiento (Sprint 2)'; Tool = 'scanner_match'; Args = @('--session', $Session) }
  $stages += @{ Name = 'Reconstrucción dispersa (Sprint 3)'; Tool = 'scanner_reconstruct'; Args = @('--session', $Session) }
}
if ($Poses -eq 'colmap') {
  # Las poses de COLMAP salen de la referencia: primero la precisión (genera colmap_ref).
  $stages += @{ Name = 'Referencia y precisión (HU-24)'; Tool = 'scanner_accuracy'; Args = @('--session', $Session) + $colmapArgs }
}
$stages += @{ Name = 'Densificación y malla (HU-19, HU-20)'; Tool = 'scanner_dense'; Args = @('--session', $Session, '--poses', $Poses) + $colmapArgs }
$stages += @{ Name = 'Textura y OBJ (HU-21, HU-22)'; Tool = 'scanner_texture'; Args = @('--session', $Session, '--poses', $Poses) + $scaleArgs }
if ($Poses -eq 'ours' -and -not $SkipAccuracy) {
  $stages += @{ Name = 'Precisión frente a COLMAP (HU-24)'; Tool = 'scanner_accuracy'; Args = @('--session', $Session) + $colmapArgs }
}

$summary = @()
$total = [Diagnostics.Stopwatch]::StartNew()
foreach ($s in $stages) {
  Write-Host "`n=== $($s.Name): $($s.Tool) $($s.Args -join ' ')" -ForegroundColor Cyan
  $watch = [Diagnostics.Stopwatch]::StartNew()
  & (Join-Path $bin "$($s.Tool).exe") @($s.Args)
  $code = $LASTEXITCODE
  $watch.Stop()
  $summary += [pscustomobject]@{ Etapa = $s.Name; Segundos = [math]::Round($watch.Elapsed.TotalSeconds, 1); Resultado = $(if ($code -eq 0) { 'ok' } else { "falló ($code)" }) }
  if ($code -ne 0) {
    $summary | Format-Table -AutoSize | Out-String | Write-Host
    Write-Host "La etapa '$($s.Name)' falló con código $code. Revisa los mensajes de arriba." -ForegroundColor Red
    exit $code
  }
}
$total.Stop()
Write-Host "`n=== Resumen" -ForegroundColor Green
$summary | Format-Table -AutoSize | Out-String | Write-Host
Write-Host ("Total: {0:N1} s. Modelo: {1}" -f $total.Elapsed.TotalSeconds, (Join-Path $Session $(if ($Poses -eq 'ours') { 'model\model.obj' } else { 'model_colmap\model.obj' })))
