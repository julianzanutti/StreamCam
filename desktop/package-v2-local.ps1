param(
    [string]$Version = '0.3.15-preview',
    [string]$PackageName = 'StreamCam-v2-0.3.15-preview',
    [string]$ReleaseName = 'v2-0.3.15-preview'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Versión inválida.' }
foreach($name in @($PackageName,$ReleaseName)) { if($name -notmatch '^[A-Za-z0-9._-]+$') { throw 'Nombre de destino inválido.' } }
$root = Split-Path $PSScriptRoot -Parent
$package = [IO.Path]::GetFullPath((Join-Path $root "dist/$PackageName"))
$release = [IO.Path]::GetFullPath((Join-Path $root "dist/$ReleaseName"))
foreach ($destination in @($package,$release)) {
    if (!$destination.StartsWith([IO.Path]::GetFullPath((Join-Path $root 'dist'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Destino fuera de dist: $destination"
    }
    if (Test-Path -LiteralPath $destination) { throw "El destino ya existe; se conserva para no borrar datos: $destination" }
}
$previous = Join-Path $root 'dist/StreamCam-preview'
$executable = Join-Path $root 'desktop/out-v2/Release/StreamCamDesktop.exe'
$apk = Join-Path $root 'android/app/build/outputs/apk/debug/app-debug.apk'
$plugin = Join-Path $root 'desktop/out-v2/obs-plugin-v2/Release/streamcam-source.dll'
$identity = Join-Path $root 'desktop/out-v2/Release/create-identity.ps1'
$compiler = Join-Path $root '.tooling/innosetup/ISCC.exe'
$setupPath = Join-Path $release ('StreamCam-'+$Version+'-windows-x64-setup.exe')
foreach ($file in @($previous,$executable,$apk,$plugin,$identity,$compiler)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Falta componente del paquete V2: $file" }
}

New-Item -ItemType Directory -Force $package,$release | Out-Null
if (Test-Path -LiteralPath $setupPath) { Remove-Item -LiteralPath $setupPath -Force }
Get-ChildItem -LiteralPath $previous -Force | Copy-Item -Destination $package -Recurse -Force
Copy-Item -LiteralPath $executable -Destination (Join-Path $package 'StreamCam.exe') -Force
Copy-Item -LiteralPath $identity -Destination $package -Force
Get-ChildItem -LiteralPath (Split-Path $executable) -Filter '*.dll' -File | Copy-Item -Destination $package -Force
foreach ($folder in @('platforms','styles','tls','imageformats','generic','networkinformation')) {
    $source = Join-Path (Split-Path $executable) $folder
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $package -Recurse -Force }
}
Copy-Item -LiteralPath $apk -Destination (Join-Path $package 'android/StreamCam-v2-local-debug.apk') -Force
Copy-Item -LiteralPath $apk -Destination (Join-Path $release "StreamCam-$Version-android-preview.apk") -Force
Copy-Item -LiteralPath $plugin -Destination (Join-Path $package 'obs-plugin/streamcam-source.dll') -Force
Copy-Item -LiteralPath (Join-Path $root 'obs-plugin/install.ps1') -Destination (Join-Path $package 'obs-plugin/install.ps1') -Force
$packageReadme = (Get-Content -Raw (Join-Path $root 'README.md')).Replace('# StreamCam',"# StreamCam`r`n`r`n> Carpeta local de trabajo $Version. Consultá GitHub Releases para verificar las versiones publicadas. Este paquete es preliminar y conserva las limitaciones documentadas.")
Set-Content -LiteralPath (Join-Path $package 'README.md') -Value $packageReadme -Encoding utf8
New-Item -ItemType Directory -Force (Join-Path $package 'docs/screenshots') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'docs/screenshots/windows-conexion.jpg') -Destination (Join-Path $package 'docs/screenshots') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/screenshots/windows-voz.jpg') -Destination (Join-Path $package 'docs/screenshots') -Force
Copy-Item -LiteralPath (Join-Path $root 'EMPEZAR.md') -Destination (Join-Path $package 'EMPEZAR.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALACION.md') -Destination (Join-Path $package 'LEEME.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/SOLUCION_DE_PROBLEMAS.md') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/PRUEBA_INTEGRACION.md') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $package -Force

$check = Start-Process -FilePath (Join-Path $package 'StreamCam.exe') -ArgumentList '--check-install' -Wait -PassThru -WindowStyle Hidden
if ($check.ExitCode -ne 0) { throw 'El paquete Windows V2 no pasó --check-install.' }
$entries = Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object Name -NE 'SHA256SUMS.txt'
$entries | Sort-Object FullName | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = $_.FullName.Substring($package.TrimEnd([IO.Path]::DirectorySeparatorChar).Length + 1)
    "$hash  $relative"
} | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding utf8

$versionInfo = ($Version -replace '-.*$','')+'.0'
& $compiler /Q "/DAppVersion=$Version" "/DFileVersion=$versionInfo" "/DPackageDir=$package" "/DOutputDirPath=$release" (Join-Path $PSScriptRoot 'installer/StreamCam.iss')
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del instalador V2 local.' }
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $release -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/V2_DEVELOPMENT_PLAN.md') -Destination (Join-Path $release 'V2-ESTADO-Y-LIMITES.md') -Force
Get-ChildItem -LiteralPath $release -File | Where-Object Name -NE 'SHA256SUMS.txt' | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding utf8
Write-Output "Paquete Windows V2 local verificado: $package"
Write-Output "Instalador local y APK de desarrollo: $release"
Write-Output 'No publicar estos artefactos como V2 estable: el APK es debug y falta QA física.'
