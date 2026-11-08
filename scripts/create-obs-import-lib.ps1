param(
    [Parameter(Mandatory = $true)][string]$ObsDll,
    [Parameter(Mandatory = $true)][string]$OutputLibrary,
    [Parameter(Mandatory = $true)][string]$OutputDefinition
)

$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $ObsDll -PathType Leaf)) { throw "OBS DLL not found: $ObsDll" }
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
$lib = Get-Command lib.exe -ErrorAction SilentlyContinue
if (!$dumpbin -or !$lib) {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Could not find dumpbin/lib on PATH or locate Visual Studio with vswhere.' }
    $installation = & $vswhere -latest -products '*' -property installationPath
    if (!$installation) { throw 'Visual Studio installation was not found.' }
    $tools = Join-Path $installation 'VC/Tools/MSVC'
    $version = Get-ChildItem -LiteralPath $tools -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (!$version) { throw 'MSVC compiler tools were not found.' }
    $toolDirectory = Join-Path $version.FullName 'bin/Hostx64/x64'
    $dumpbin = Get-Item (Join-Path $toolDirectory 'dumpbin.exe') -ErrorAction Stop
    $lib = Get-Item (Join-Path $toolDirectory 'lib.exe') -ErrorAction Stop
}
$exports = & $dumpbin.Source /nologo /exports $ObsDll
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed to read OBS exports.' }
$names = @(
    foreach ($line in $exports) {
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') { $Matches[1] }
    }
)
$names = @($names | Sort-Object -Unique)
if ($names.Count -lt 100) { throw "Unexpected OBS export count: $($names.Count)" }

$definitionDirectory = Split-Path -Parent $OutputDefinition
$libraryDirectory = Split-Path -Parent $OutputLibrary
New-Item -ItemType Directory -Force $definitionDirectory, $libraryDirectory | Out-Null
@('LIBRARY obs.dll', 'EXPORTS') + $names | Set-Content -LiteralPath $OutputDefinition -Encoding ascii
& $lib.Source /nologo "/def:$OutputDefinition" /machine:x64 "/out:$OutputLibrary"
if ($LASTEXITCODE -ne 0) { throw 'lib.exe failed to generate the OBS import library.' }
Write-Host "Generated OBS import library with $($names.Count) exports: $OutputLibrary"
