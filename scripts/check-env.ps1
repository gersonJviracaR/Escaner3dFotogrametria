# Revisa las herramientas necesarias para el proyecto y muestra una tabla.
# Uso: powershell -ExecutionPolicy Bypass -File scripts\check-env.ps1

# Recarga PATH y VCPKG_ROOT del registro (por si se instaló algo sin abrir una consola nueva).
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable("VCPKG_ROOT", "User") }

function Get-FirstLine([scriptblock]$Block) {
    try {
        $out = & $Block 2>&1 | Where-Object { "$_".Trim() -ne "" } | Select-Object -First 1
        if ($null -eq $out) { return $null }
        return "$out".Trim()
    } catch { return $null }
}

function Test-Cmd([string]$Name) { [bool](Get-Command $Name -ErrorAction SilentlyContinue) }

$rows = @()
function Add-Row($Tool, $Version, $Hint) {
    $script:rows += [pscustomobject]@{
        Herramienta = $Tool
        Instalada   = if ($Version) { "Sí" } else { "No" }
        Version     = if ($Version) { $Version } else { "-" }
        Accion      = if ($Version) { "-" } else { $Hint }
    }
}

Add-Row "Git" $(if (Test-Cmd git) { Get-FirstLine { git --version } }) "winget install --id Git.Git -e"
Add-Row "CMake" $(if (Test-Cmd cmake) { Get-FirstLine { cmake --version } }) "winget install --id Kitware.CMake -e"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvc = $null
if (Test-Path $vswhere) {
    $msvc = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productDisplayVersion | Select-Object -First 1
    if ($msvc) { $msvc = "VS Build Tools $msvc" }
}
Add-Row "MSVC (VS 2022)" $msvc "winget install --id Microsoft.VisualStudio.2022.BuildTools -e (ver README)"

$vcpkgExe = if ($env:VCPKG_ROOT) { Join-Path $env:VCPKG_ROOT "vcpkg.exe" } else { "C:\vcpkg\vcpkg.exe" }
Add-Row "vcpkg" $(if (Test-Path $vcpkgExe) { (Get-FirstLine { & $vcpkgExe version }) -replace '.*version ', '' }) "git clone https://github.com/microsoft/vcpkg C:\vcpkg"
Add-Row "VCPKG_ROOT" $env:VCPKG_ROOT "[Environment]::SetEnvironmentVariable('VCPKG_ROOT','C:\vcpkg','User')"
Add-Row "Flutter" $(if (Test-Cmd flutter) { Get-FirstLine { flutter --version } }) "git clone https://github.com/flutter/flutter.git -b stable C:\src\flutter"
Add-Row "adb" $(if (Test-Cmd adb) { Get-FirstLine { adb --version } }) "Instalar Android Studio y su SDK"
Add-Row "Java" $(if (Test-Cmd java) { Get-FirstLine { java -version } }) "Lo trae Android Studio"
Add-Row "GitHub CLI" $(if (Test-Cmd gh) { Get-FirstLine { gh --version } }) "Opcional: winget install --id GitHub.cli -e"

$colmapBat = "C:\tools\colmap\COLMAP.bat"
$colmap = if (Test-Path $colmapBat) { "C:\tools\colmap" } elseif (Test-Cmd colmap) { (Get-Command colmap).Source } else { $null }
Add-Row "COLMAP" $colmap "Descargar zip de https://github.com/colmap/colmap/releases a C:\tools\colmap"

$gpu = $null
if (Test-Cmd nvidia-smi) { $gpu = Get-FirstLine { nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader } }
Add-Row "GPU NVIDIA (CUDA)" $gpu "Sin GPU NVIDIA: la densificación de COLMAP (Sprint 4) será limitada"

$rows | Format-Table -AutoSize -Wrap
