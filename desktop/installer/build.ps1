param(
    [string]$Version = '0.2.0-preview',
    [string]$Compiler
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Versión inválida.' }
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (!$Compiler) { $Compiler = Join-Path $root '.tooling/innosetup/ISCC.exe' }
if (!(Test-Path -LiteralPath $Compiler)) { throw 'Falta Inno Setup. Pasá -Compiler con la ruta a ISCC.exe.' }
$package = Join-Path $root 'dist/StreamCam-preview'
$check = Start-Process -FilePath (Join-Path $package 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
if ($check.ExitCode -ne 0) { throw 'El paquete no pasó la comprobación de instalación.' }
$output = Join-Path $root "dist/release-$Version"
New-Item -ItemType Directory -Force $output | Out-Null
& $Compiler /Q "/DAppVersion=$Version" "/DPackageDir=$package" "/DOutputDirPath=$output" (Join-Path $PSScriptRoot 'StreamCam.iss')
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del instalador.' }
Write-Output "Instalador generado en $output"
