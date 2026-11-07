param(
    [Parameter(Mandatory=$true)][string]$QtPath,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$qtConfig = Join-Path $QtPath 'lib\cmake\Qt6\Qt6Config.cmake'
if (!(Test-Path -LiteralPath $qtConfig)) { throw "No se encontró Qt6 en $QtPath" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Falta instalar Visual Studio Build Tools C++.' }
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsInstall) { throw 'Falta el componente MSVC x64.' }
$cmake = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { throw 'Falta el componente CMake de Visual Studio.' }
$buildDir = Join-Path $PSScriptRoot 'out'
& $cmake -S (Join-Path $PSScriptRoot 'app') -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPath"
if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración CMake.' }
& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Windows.' }
$exe = Join-Path $buildDir "$Configuration\StreamCamDesktop.exe"
$previousVcInstallDir = $env:VCINSTALLDIR
try {
    $env:VCINSTALLDIR = Join-Path $vsInstall 'VC'
    & (Join-Path $QtPath 'bin\windeployqt.exe') "--$($Configuration.ToLowerInvariant())" --no-translations --compiler-runtime $exe
    if ($LASTEXITCODE -ne 0) { throw 'Falló el empaquetado Qt.' }
} finally { $env:VCINSTALLDIR = $previousVcInstallDir }
Write-Output "Aplicación: $exe"
