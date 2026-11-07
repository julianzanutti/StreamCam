param([string]$Version = '0.2.2-preview')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Versión inválida.' }
$root = Split-Path $PSScriptRoot -Parent
$setup = Join-Path $root "dist/release-$Version/StreamCam-$Version-windows-x64-setup.exe"
$testDirectory = Join-Path $root '.tooling/installer smoke test'
$log = Join-Path $root '.tooling/installer-smoke.log'
if (!(Test-Path -LiteralPath $setup)) { throw 'Falta el instalador.' }
if (Test-Path -LiteralPath $testDirectory) { throw 'La carpeta de prueba ya existe; revisala antes de volver a probar.' }
# Refuse to replace an actual installation registered under the stable app ID.
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{D8C48776-49C9-4B11-986A-21CB99676586}_is1'
if (Test-Path $uninstallKey) { throw 'Ya hay una instalación registrada; no se reemplaza en esta prueba.' }
$arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',"/DIR=`"$testDirectory`"",'/GROUP="StreamCam Installer Test"','/TASKS=""',"/LOG=`"$log`"")
for ($attempt=1; $attempt -le 2; $attempt++) {
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Falló instalación/reinstalación: $($process.ExitCode)" }
    $check = Start-Process -FilePath (Join-Path $testDirectory 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
    if ($check.ExitCode -ne 0) { throw 'Falló la comprobación de la app instalada.' }
    $registered = Get-ItemProperty $uninstallKey
    if ($registered.DisplayVersion -ne $Version) { throw 'Versión registrada incorrecta.' }
    Write-Output "PASS: instalación $attempt y comprobación de identidad/USB."
}
$previousPolicy = $env:PSExecutionPolicyPreference
$previousPath = $env:PATH
try {
    Set-Content -LiteralPath (Join-Path $testDirectory 'create-identity.ps1') -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"
    $env:PSExecutionPolicyPreference = 'AllSigned'
    $env:PATH = Join-Path $env:SystemRoot 'System32'
    $check = Start-Process -FilePath (Join-Path $testDirectory 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
    if ($check.ExitCode -ne 0) { throw 'Falló el paquete con PATH mínimo y script descargado bajo AllSigned.' }
    Write-Output 'PASS: paquete autónomo con PATH mínimo, script descargado y AllSigned.'
} finally {
    $env:PSExecutionPolicyPreference = $previousPolicy
    $env:PATH = $previousPath
}
Write-Output "PASS: instalación limpia y reinstalación en ruta con espacios. Carpeta para inspección visual: $testDirectory"
Write-Output 'Después de inspeccionar, ejecutá unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART y verificá que se quitó la app.'
