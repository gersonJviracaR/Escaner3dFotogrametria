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

# Mínimo de cámaras registradas por el Sprint 3 para seguir a la densificación.
$MinRegisteredCameras = 4

# Devuelve $null si la reconstrucción dispersa alcanza para densificar; si no, el motivo.
function Test-SparseReconstruction([string]$sessionDir) {
  $file = Join-Path $sessionDir 'reconstruction\reconstruction_report.json'
  if (-not (Test-Path $file)) { return "no existe $file (ejecuta antes scanner_reconstruct)." }
  $r = Get-Content $file -Raw -Encoding UTF8 | ConvertFrom-Json
  $registered = [int]$r.summary.cameras_registered
  if ($registered -ge $MinRegisteredCameras) { return $null }
  $lines = @("La reconstrucción dispersa registró $registered de $($r.summary.cameras_candidates) cámaras candidatas; hacen falta al menos $MinRegisteredCameras para densificar.")
  if ($r.initial_pair.i -lt 0) {
    $lines += 'Motivo: ningún par cumplió las condiciones del par inicial (HU-13):'
    foreach ($e in $r.initial_pair.evaluated | Select-Object -First 5) {
      $lines += ("  par {0}-{1}: {2} inliers, ángulo mediano {3:N2}°, {4}" -f $e.i, $e.j, $e.n_inliers, $e.median_angle_deg, $e.result)
    }
  } elseif ([int]$r.points.before_filter -eq 0) {
    $lines += ("Motivo: el par inicial {0}-{1} se eligió, pero no dejó ningún punto triangulado: todas sus pistas fallaron los controles de HU-14 (detrás de la cámara, ángulo < tri_min_angle_deg o error > max_reproj_error_px = {2} px con la K inicial, f = {3:N1} px)." -f $r.initial_pair.i, $r.initial_pair.j, $r.params.max_reproj_error_px, $r.intrinsics.initial.f)
    $lines += 'Sin puntos 3D no hay correspondencias 2D-3D para registrar las demás cámaras con PnP.'
  } else {
    $lines += "Motivo: el registro con PnP se detuvo; las cámaras restantes no alcanzaron pnp_min_inliers = $($r.params.pnp_min_inliers) correspondencias 2D-3D."
  }
  $reasons = $r.cameras | Where-Object { -not $_.registered } | Group-Object reason | ForEach-Object { "$($_.Name): $($_.Count)" }
  if ($reasons) { $lines += "Cámaras no registradas por motivo: $($reasons -join ', ')." }
  $lines += "Detalle en $file y en la salida de scanner_reconstruct de arriba."
  return ($lines -join "`n")
}

$summary = @()
$total = [Diagnostics.Stopwatch]::StartNew()
foreach ($s in $stages) {
  if ($s.Tool -eq 'scanner_dense' -and $Poses -eq 'ours') {
    $problem = Test-SparseReconstruction $Session
    if ($problem) {
      $summary += [pscustomobject]@{ Etapa = $s.Name; Segundos = 0; Resultado = 'no ejecutada' }
      $summary | Format-Table -AutoSize | Out-String | Write-Host
      Write-Host "Pipeline detenido antes de la densificación." -ForegroundColor Red
      Write-Host $problem -ForegroundColor Red
      exit 4
    }
  }
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
