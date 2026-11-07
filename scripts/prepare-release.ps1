param([string]$Version='0.1.0-preview')
$ErrorActionPreference='Stop'
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?$') {throw 'Versión inválida.'}
$projectRoot=Split-Path $PSScriptRoot -Parent
$python=Get-Command python -ErrorAction Stop
& $python.Source (Join-Path $PSScriptRoot 'audit-public.py')
if ($LASTEXITCODE -ne 0) {throw 'La auditoría de fuente no pasó.'}
$package=Join-Path $projectRoot 'dist\StreamCam-preview'
if (!(Test-Path -LiteralPath (Join-Path $package 'SHA256SUMS.txt'))) {throw 'Ejecutar desktop/package.ps1 primero.'}
$output=Join-Path $projectRoot "dist\release-$Version"
New-Item -ItemType Directory -Force $output | Out-Null
$windows=Join-Path $output "StreamCam-$Version-windows-x64.zip"
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $windows -Force
Copy-Item -LiteralPath (Join-Path $package 'android\StreamCam-debug.apk') -Destination (Join-Path $output "StreamCam-$Version-android-debug.apk") -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $output -Force
Get-ChildItem -LiteralPath $output -File | Where-Object Name -NE 'SHA256SUMS.txt' | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding utf8
Write-Output "Artefactos locales de prueba: $output. No se publicó nada. APK debug: no presentar como release estable."
