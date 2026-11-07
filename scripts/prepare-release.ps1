param([string]$Version='0.2.2-preview', [switch]$Installer, [switch]$SkipPortable)
$ErrorActionPreference='Stop'
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?$') {throw 'Versión inválida.'}
$projectRoot=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'audit-public.ps1')
if ($LASTEXITCODE -ne 0) {throw 'La auditoría de fuente no pasó.'}
$package=Join-Path $projectRoot 'dist\StreamCam-preview'
if (!(Test-Path -LiteralPath (Join-Path $package 'SHA256SUMS.txt'))) {throw 'Ejecutar desktop/package.ps1 primero.'}
$manifest=Join-Path $package 'SHA256SUMS.txt'
foreach ($line in Get-Content -LiteralPath $manifest) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Línea inválida en SHA256SUMS.txt: $line" }
    $file=Join-Path $package $Matches[2]
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Falta un archivo del paquete: $($Matches[2])" }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $Matches[1]) { throw "Hash incorrecto: $($Matches[2]). Volvé a ejecutar desktop/package.ps1." }
}
$required=@('StreamCam.exe','create-identity.ps1','vc_redist.x64.exe','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','EMPEZAR.txt','obs-plugin/streamcam-source.dll','obs-plugin/install.ps1','runtime/powershell/pwsh.exe','runtime/adb/adb.exe','android/StreamCam-preview.apk')
foreach ($relative in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "El paquete está incompleto: falta $relative. Volvé a ejecutar desktop/package.ps1." }
}
$output=[IO.Path]::GetFullPath((Join-Path $projectRoot "dist\release-$Version"))
$distributionRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))+[IO.Path]::DirectorySeparatorChar
if (!$output.StartsWith($distributionRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Destino de publicación fuera de dist.' }
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
New-Item -ItemType Directory -Force $output | Out-Null
$windows=Join-Path $output "StreamCam-$Version-windows-x64.zip"
if (!$SkipPortable) { Compress-Archive -Path (Join-Path $package '*') -DestinationPath $windows -Force }
if ($Installer) {
    & (Join-Path $projectRoot 'desktop/installer/build.ps1') -Version $Version
    if ($LASTEXITCODE -ne 0) { throw 'Falló la preparación del instalador.' }
}
Copy-Item -LiteralPath (Join-Path $package 'android\StreamCam-preview.apk') -Destination (Join-Path $output "StreamCam-$Version-android-preview.apk") -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $output -Force
Get-ChildItem -LiteralPath $output -File | Where-Object Name -NE 'SHA256SUMS.txt' | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding utf8
Write-Output "Artefactos locales de prueba: $output. No se publicó nada. APK debug: no presentar como release estable."
