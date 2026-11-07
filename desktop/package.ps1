$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$release = Join-Path $PSScriptRoot 'out\Release'
$destination = Join-Path $projectRoot 'dist\StreamCam-preview'
$required = @(
    (Join-Path $release 'StreamCamDesktop.exe'),
    (Join-Path $release 'create-identity.ps1'),
    (Join-Path $projectRoot 'android\app\build\outputs\apk\debug\app-debug.apk'),
    (Join-Path $projectRoot 'obs-plugin\out\Release\streamcam-source.dll')
    (Join-Path $release 'vc_redist.x64.exe')
)
foreach ($file in $required) { if (!(Test-Path -LiteralPath $file)) { throw "Falta artefacto: $file" } }
if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
$powerShellSource = Split-Path (Get-Command pwsh -ErrorAction Stop).Source -Parent
$runtimeParent = Join-Path $destination 'runtime'
New-Item -ItemType Directory -Force $runtimeParent | Out-Null
$adbSource = Join-Path $projectRoot '.android-sdk\platform-tools'
$adbDestination = Join-Path $runtimeParent 'adb'
New-Item -ItemType Directory -Force $adbDestination | Out-Null
foreach ($file in @('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt')) {
    $source = Join-Path $adbSource $file
    if (!(Test-Path -LiteralPath $source)) { throw "Falta Android Platform Tools: $file" }
    Copy-Item -LiteralPath $source -Destination $adbDestination -Force
}
$runtimeDestination = Join-Path $runtimeParent 'powershell'
New-Item -ItemType Directory -Force $runtimeDestination | Out-Null
Get-ChildItem -LiteralPath $powerShellSource -Recurse -File -Force | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($powerShellSource,$_.FullName)
    $target = Join-Path $runtimeDestination $relative
    $existing = Get-Item -LiteralPath $target -ErrorAction SilentlyContinue
    if (!$existing -or $existing.Length -ne $_.Length -or $existing.LastWriteTimeUtc -ne $_.LastWriteTimeUtc) {
        New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}
Copy-Item -LiteralPath $required[0] -Destination (Join-Path $destination 'StreamCam.exe') -Force
Copy-Item -LiteralPath $required[1] -Destination $destination -Force
Copy-Item -LiteralPath $required[4] -Destination $destination -Force
Get-ChildItem -LiteralPath $release -Filter '*.dll' -File | Copy-Item -Destination $destination -Force
foreach ($folder in @('platforms','styles','tls','imageformats','generic','networkinformation')) {
    $source = Join-Path $release $folder
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force }
}
$android = Join-Path $destination 'android'
$obs = Join-Path $destination 'obs-plugin'
New-Item -ItemType Directory -Force $android,$obs | Out-Null
Copy-Item -LiteralPath $required[2] -Destination (Join-Path $android 'StreamCam-preview.apk') -Force
Copy-Item -LiteralPath $required[3] -Destination $obs -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'obs-plugin\install.ps1') -Destination $obs -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\INSTALACION.md') -Destination (Join-Path $destination 'LEEME.md') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\SOLUCION_DE_PROBLEMAS.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\PRUEBA_INTEGRACION.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'EMPEZAR.md') -Destination (Join-Path $destination 'EMPEZAR.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $destination -Force
$notices=Join-Path $destination 'notices' 
New-Item -ItemType Directory -Force $notices | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\THIRD_PARTY.md') -Destination $notices -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.tooling\webrtc-msvc\NOTICE') -Destination (Join-Path $notices 'webrtc-NOTICE.txt') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'app\third_party\qrcodegen.hpp') -Destination (Join-Path $notices 'nayuki-qrcodegen.hpp') -Force
$files = Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object Name -NE 'SHA256SUMS.txt'
$files | Sort-Object FullName | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = [IO.Path]::GetRelativePath($destination,$_.FullName)
    "$hash  $relative"
} | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt')
$distributionRoot = Join-Path $projectRoot 'dist'
$archive = Join-Path $distributionRoot 'StreamCam-Windows-x64-preview.zip'
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $archive -Force
Copy-Item -LiteralPath (Join-Path $android 'StreamCam-preview.apk') -Destination (Join-Path $distributionRoot 'StreamCam-preview.apk') -Force
Write-Output "Paquete Windows completo: $archive"
Write-Output "APK Android de prueba: $(Join-Path $distributionRoot 'StreamCam-preview.apk')"
Write-Output "Carpeta descomprimida (StreamCam.exe requiere sus DLL vecinas): $destination"
