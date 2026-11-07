param([string]$PluginDll)
$ErrorActionPreference = 'Stop'
if (!$PluginDll) {
    $PluginDll = Join-Path $PSScriptRoot 'streamcam-source.dll'
    if (!(Test-Path -LiteralPath $PluginDll)) { $PluginDll = Join-Path $PSScriptRoot 'out\Release\streamcam-source.dll' }
}
if (!(Test-Path -LiteralPath $PluginDll -PathType Leaf)) { throw "No existe el plugin compilado: $PluginDll" }
if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'Cerrá OBS antes de instalar el plugin y volvé a ejecutar este script.' }
$pluginDirectory = Join-Path $env:APPDATA 'obs-studio\plugins\streamcam-source'
$binaryDirectory = Join-Path $pluginDirectory 'bin\64bit'
New-Item -ItemType Directory -Force $binaryDirectory,(Join-Path $pluginDirectory 'data') | Out-Null
Copy-Item -LiteralPath $PluginDll -Destination (Join-Path $binaryDirectory 'streamcam-source.dll') -Force
Write-Output "Instalado para este usuario: $binaryDirectory. Abrí OBS y agregá la fuente StreamCam."
