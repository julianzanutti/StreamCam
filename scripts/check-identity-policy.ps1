$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$fixtureDirectory = Join-Path $projectRoot '.tooling/identity-policy-test'
New-Item -ItemType Directory -Force $fixtureDirectory | Out-Null
$script = Join-Path $fixtureDirectory 'create-identity.ps1'
Copy-Item -LiteralPath (Join-Path $projectRoot 'desktop/app/create-identity.ps1') -Destination $script -Force
# Reproduce the mark Windows puts on files extracted from a downloaded ZIP.
Set-Content -LiteralPath $script -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"
$shell = (Get-Command pwsh -ErrorAction Stop).Source
$previousPolicy = $env:PSExecutionPolicyPreference
$previousPath = $env:PATH
try {
    $env:PSExecutionPolicyPreference = 'AllSigned'
    $blocked = & $shell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy AllSigned -File $script -Directory (Join-Path $fixtureDirectory 'blocked') 2>&1
    if ($LASTEXITCODE -eq 0 -or "$blocked" -notmatch 'digitally signed') {
        throw 'No se reprodujo el bloqueo esperado de firma de PowerShell.'
    }
    $blocked = $null
    $env:PATH = (Split-Path $shell -Parent) + [IO.Path]::PathSeparator + $previousPath
    & (Join-Path $projectRoot 'desktop/out/Release/StreamCamIdentityProbe.exe') $script
    if ($LASTEXITCODE -ne 0) { throw 'Falló la regresión de identidad bajo AllSigned.' }
    Write-Output 'PASS: script descargado bloqueado sin corrección; creación y recarga aprobadas con la invocación de StreamCam.'
} finally {
    $env:PSExecutionPolicyPreference = $previousPolicy
    $env:PATH = $previousPath
}
