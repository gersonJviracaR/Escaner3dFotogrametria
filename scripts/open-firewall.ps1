# Abre el puerto del servidor en el cortafuegos de Windows (perfil Privado).
# Requiere PowerShell como administrador. La red Wi-Fi debe estar marcada como "Privada".
param([int]$Port = 8080)

$name = "Escaner3D Servidor"
$existing = Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "La regla '$name' ya existe; se actualiza el puerto a $Port."
    $existing | Get-NetFirewallPortFilter | Set-NetFirewallPortFilter -LocalPort $Port
} else {
    New-NetFirewallRule -DisplayName $name -Direction Inbound -Protocol TCP -LocalPort $Port -Action Allow -Profile Private | Out-Null
    Write-Host "Regla '$name' creada: entrada TCP $Port, perfil Privado."
}
Get-NetConnectionProfile | Select-Object Name, InterfaceAlias, NetworkCategory | Format-Table -AutoSize
