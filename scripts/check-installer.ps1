param(
    [string]$Version = '0.3.13-preview',
    [string]$PackagePath = 'dist/StreamCam-v2-0.3.13-preview',
    [string]$CompilerPath
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Versión inválida.' }
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$toolingRoot = [IO.Path]::GetFullPath((Join-Path $root '.tooling')).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$package = [IO.Path]::GetFullPath((Join-Path $root $PackagePath))
$compiler = if($CompilerPath) { [IO.Path]::GetFullPath($CompilerPath) } else { Join-Path $root '.tooling/innosetup/ISCC.exe' }
$runId = [guid]::NewGuid().ToString('N')
$testDirectory = Join-Path $toolingRoot ("installer-smoke-$Version-$runId")
$installDirectory = Join-Path $testDirectory 'installed app'
$setup = Join-Path $testDirectory "StreamCam-$Version-isolated-smoke-setup.exe"
$log = Join-Path $testDirectory 'install.log'
$diagnosticLog = Join-Path $toolingRoot "installer-smoke-$Version-$runId.log"
if (!$package.StartsWith($root.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'La carpeta del paquete debe estar dentro del proyecto.'
}
if (!(Test-Path -LiteralPath $package -PathType Container)) { throw "Falta la carpeta del paquete Windows: $package" }
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "Falta Inno Setup: $compiler" }
$iss = Join-Path $root 'desktop/installer/StreamCam.iss'
$smokeAppId = '{{' + [guid]::NewGuid().ToString('D').ToUpperInvariant() + '}'
$fileVersion = ($Version -replace '-.*$','')+'.0'
$testGuid = $smokeAppId.Substring(2).TrimEnd('}')
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{$testGuid}_is1"
$previousPolicy = $env:PSExecutionPolicyPreference
$previousPath = $env:PATH
$installed = $false
$installAttempted = $false
$passed = $false
New-Item -ItemType Directory -Path $testDirectory | Out-Null
try {
    & $compiler /Q "/DAppVersion=$Version" "/DFileVersion=$fileVersion" "/DPackageDir=$package" "/DOutputDirPath=$testDirectory" "/DSmokeAppId=$smokeAppId" $iss
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $setup)) { throw 'No se pudo compilar el instalador de prueba aislado.' }
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',"/DIR=`"$installDirectory`"",'/GROUP="StreamCam Installer Test"','/TASKS=""',"/LOG=`"$log`"")
    for ($attempt=1; $attempt -le 2; $attempt++) {
        $installAttempted = $true
        $process = Start-Process -FilePath $setup -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0) { throw "Falló instalación/reinstalación: $($process.ExitCode)" }
        $installed = $true
        $check = Start-Process -FilePath (Join-Path $installDirectory 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
        if ($check.ExitCode -ne 0) { throw 'Falló la comprobación de la app instalada.' }
        $registered = Get-ItemProperty -LiteralPath $uninstallKey
        $registeredInstall = [IO.Path]::GetFullPath([string]$registered.InstallLocation).TrimEnd([IO.Path]::DirectorySeparatorChar)
        $expectedInstall = $installDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar)
        if ($registered.DisplayVersion -ne $Version -or $registeredInstall -ne $expectedInstall) { throw 'Versión o ruta registrada incorrecta.' }
        Write-Output "PASS: instalación $attempt y comprobación de identidad/USB."
    }
    try {
        Set-Content -LiteralPath (Join-Path $installDirectory 'create-identity.ps1') -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"
        $env:PSExecutionPolicyPreference = 'AllSigned'
        $env:PATH = Join-Path $env:SystemRoot 'System32'
        $check = Start-Process -FilePath (Join-Path $installDirectory 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
        if ($check.ExitCode -ne 0) { throw 'Falló el paquete con PATH mínimo y script descargado bajo AllSigned.' }
        Write-Output 'PASS: paquete autónomo con PATH mínimo, script descargado y AllSigned.'
    } finally {
        $env:PSExecutionPolicyPreference = $previousPolicy
        $env:PATH = $previousPath
    }
    $uninstaller = Join-Path $installDirectory 'unins000.exe'
    if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) { throw 'No se creó el desinstalador.' }
    $uninstall = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -Wait -PassThru -WindowStyle Hidden
    if ($uninstall.ExitCode -ne 0) { throw "Falló la desinstalación aislada: $($uninstall.ExitCode)" }
    $installed = $false
    if (Test-Path -LiteralPath $uninstallKey) { throw 'La clave de desinstalación siguió registrada.' }
    if (Test-Path -LiteralPath $installDirectory) { throw 'Quedaron archivos en la carpeta de instalación.' }
    $passed = $true
    Write-Output 'PASS: instalación, reinstalación, comprobación y desinstalación aisladas.'
} finally {
    $env:PSExecutionPolicyPreference = $previousPolicy
    $env:PATH = $previousPath
    if (Test-Path -LiteralPath $log -PathType Leaf) { Copy-Item -LiteralPath $log -Destination $diagnosticLog -Force }
    if ($installed -and (Test-Path -LiteralPath (Join-Path $installDirectory 'unins000.exe') -PathType Leaf)) {
        $uninstaller = Join-Path $installDirectory 'unins000.exe'
        $cleanup = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -Wait -PassThru -WindowStyle Hidden
        if ($cleanup.ExitCode -ne 0) { Write-Warning "La limpieza de la instalación aislada devolvió $($cleanup.ExitCode)." }
        else { $installed = $false }
    }
    if ($installAttempted -and $installed -and (Test-Path -LiteralPath $uninstallKey)) {
        Remove-Item -LiteralPath $uninstallKey -Recurse -Force
    }
    $safeTestPath = [IO.Path]::GetFullPath($testDirectory)
    if ($safeTestPath.StartsWith($toolingRoot,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $safeTestPath)) {
        Remove-Item -LiteralPath $safeTestPath -Recurse -Force
    }
}
if (!$passed) { throw "El smoke del instalador no terminó; el log queda en $diagnosticLog" }
Write-Output "PASS: ciclo completo aislado en G:. No se modificó la instalación normal. Log: $diagnosticLog"
