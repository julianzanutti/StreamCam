param(
    [string]$AdbSerial = 'emulator-5554',
    [string]$ApkPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'android/app/build/outputs/apk/debug/app-debug.apk'),
    [string]$AuthProbePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'desktop/out-v2/Release/StreamCamAuthProbe.exe'),
    [string]$PowerShellRuntime = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist/StreamCam-v2-0.3.13-preview/runtime/powershell')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$adb = Join-Path $root '.tooling/android-sdk/platform-tools/adb.exe'
foreach ($file in @($adb,$ApkPath,$AuthProbePath,(Join-Path $PowerShellRuntime 'pwsh.exe'))) {
    if (!(Test-Path -LiteralPath $file)) { throw "Falta el componente de prueba: $file" }
}
if ($AdbSerial -notmatch '^emulator-[0-9]+$') { throw 'La regresión solo acepta un AVD, no un teléfono físico.' }
$temp = Join-Path $root ('.tooling/usb-ui-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$stdout = Join-Path $temp 'authprobe.stdout.log'
$stderr = Join-Path $temp 'authprobe.stderr.log'
$oldPath = $env:Path
$oldUsb = $env:STREAMCAM_AUTH_USB
$oldReport = $env:STREAMCAM_AUTH_REPORT_USB_CODE
$oldIdentityDir = $env:STREAMCAM_AUTH_IDENTITY_DIR
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$probe = $null
$reverseAdded = $false
$passed = $false

function Invoke-Adb([string[]]$arguments) {
    $output = & $script:adb -s $script:AdbSerial @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ADB falló ($LASTEXITCODE): $($output -join ' ')" }
    return $output
}
function Get-UiDocument {
    Invoke-Adb @('shell','uiautomator','dump','/sdcard/streamcam-window.xml') | Out-Null
    Invoke-Adb @('pull','/sdcard/streamcam-window.xml',(Join-Path $script:temp 'window.xml')) | Out-Null
    return [xml](Get-Content -LiteralPath (Join-Path $script:temp 'window.xml') -Raw)
}
function Get-UiNodes([xml]$document) { return $document.SelectNodes('//node') }
function Find-UiNode([xml]$document,[scriptblock]$predicate) {
    foreach ($node in (Get-UiNodes $document)) { if (& $predicate $node) { return $node } }
    return $null
}
function Tap-UiNode($node) {
    if (!$node) { throw 'No se encontró el control que se debe pulsar.' }
    if ($node.bounds -notmatch '^\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]$') { throw "Bounds UI inválidos: $($node.bounds)" }
    $x=[int](([int]$Matches[1]+[int]$Matches[3])/2); $y=[int](([int]$Matches[2]+[int]$Matches[4])/2)
    Write-Output "TAP $x,$y · $($node.text)"
    Invoke-Adb @('shell','input','tap',"$x","$y") | Out-Null
}
function Wait-ForUi([scriptblock]$predicate,[string]$description,[int]$seconds=30) {
    $deadline=(Get-Date).AddSeconds($seconds)
    do {
        $document=Get-UiDocument
        $node=Find-UiNode $document $predicate
        if ($node) { return @{ Document=$document; Node=$node } }
        Start-Sleep -Milliseconds 750
    } while ((Get-Date) -lt $deadline)
    throw "Timeout esperando UI: $description"
}

try {
    $state=(Invoke-Adb @('get-state') | Select-Object -First 1).ToString().Trim()
    if ($state -ne 'device') { throw "El AVD $AdbSerial no está listo (estado: $state)." }
    $isEmulator=(Invoke-Adb @('shell','getprop','ro.kernel.qemu') | Select-Object -First 1).ToString().Trim()
    if ($isEmulator -ne '1') { throw 'El serial indicado no corresponde a un AVD confirmado por Android.' }
    $boot=(Invoke-Adb @('shell','getprop','sys.boot_completed') | Select-Object -First 1).ToString().Trim()
    if ($boot -ne '1') { throw 'Android todavía no completó el arranque.' }

    Invoke-Adb @('install','-r',$ApkPath) | Out-Null
    Invoke-Adb @('reverse','tcp:47831','tcp:47831') | Out-Null
    $reverseAdded=$true

    $env:Path="$PowerShellRuntime;$oldPath"
    $env:STREAMCAM_AUTH_USB='1'
    $env:STREAMCAM_AUTH_REPORT_USB_CODE='1'
    $env:STREAMCAM_AUTH_IDENTITY_DIR=Join-Path $temp 'identity'
    $env:TEMP=$temp
    $env:TMP=$temp
    $probe=Start-Process -FilePath $AuthProbePath -WorkingDirectory (Split-Path $AuthProbePath) `
        -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $invite=$null
    $deadline=(Get-Date).AddSeconds(30)
    do {
        if (Test-Path -LiteralPath $stdout) {
            foreach ($line in Get-Content -LiteralPath $stdout) {
                if ($line.StartsWith('{')) {
                    $record=ConvertFrom-Json -InputObject $line
                    if ($record.type -eq 'probe.invitation') { $invite=$record; break }
                }
            }
        }
        if ($invite) { break }
        if ($probe.HasExited) { throw "AuthProbe salió ($($probe.ExitCode)): $(Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue)" }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    if (!$invite) { throw 'AuthProbe no creó una invitación USB en 30 segundos.' }
    if ($invite.probe_usb_code -notmatch '^[0-9]{6}$') { throw 'AuthProbe no generó un código de seis dígitos.' }
    $payload=$invite.payload

    Invoke-Adb @('shell','input','keyevent','82') | Out-Null
    Invoke-Adb @('shell','am','start','-W','-n','com.streamcam.diagnostics/.PairingActivity',
        '-a','com.streamcam.USB_CONNECT','--es','pc_id',[string]$payload.pc_id,'--es','pc_pin',[string]$payload.pc_key_sha256) | Out-Null
    $warning=Wait-ForUi { param($node) $node.text -like '*Android no puede verificar qué aplicación inició*' } 'advertencia de intent USB no confiable'
    $continue=Find-UiNode $warning.Document { param($node) $node.text -eq 'Continuar' }
    Tap-UiNode $continue

    $challenge=Wait-ForUi { param($node) $node.class -eq 'android.widget.EditText' -and $node.'content-desc' -eq 'Código de verificación USB' } 'campo accesible de verificación USB'
    $inputNode=$challenge.Node
    Tap-UiNode $inputNode
    $wrong=if($invite.probe_usb_code -eq '000000'){'000001'}else{'000000'}
    Invoke-Adb @('shell','input','text',$wrong) | Out-Null
    $wrongState=Wait-ForUi { param($node) $node.text -like '*No coincide*' } 'rechazo visual del código incorrecto'
    $wrongButton=Find-UiNode $wrongState.Document { param($node) $node.text -eq 'Verificar código' }
    if (!$wrongButton -or $wrongButton.enabled -ne 'false') { throw 'El código incorrecto habilitó Verificar código.' }
    if ($probe.HasExited -or (Get-Content -LiteralPath $stdout -Raw).Contains('probe.usb_accepted')) {
        throw 'El servidor aceptó el emparejamiento antes de escribir el código correcto.'
    }
    Write-Output 'PASS: el intent USB no confiable muestra advertencia y el código incorrecto no habilita la conexión.'

    Invoke-Adb @('shell','input','keyevent','123') | Out-Null
    Invoke-Adb @('shell','input','keyevent','67','67','67','67','67','67') | Out-Null
    Invoke-Adb @('shell','input','text',[string]$invite.probe_usb_code) | Out-Null
    $correctState=Wait-ForUi { param($node) $node.text -eq 'Verificar código' -and $node.enabled -eq 'true' } 'habilitación para el código exacto'
    Tap-UiNode $correctState.Node
    $submitted=Wait-ForUi { param($node) $node.text -like '*Código comparado*' -or $node.text -like 'Todo listo*' } 'envío del código exacto al servidor' 8

    $accepted=$false
    $deadline=(Get-Date).AddSeconds(25)
    do {
        if ((Test-Path -LiteralPath $stdout) -and (Get-Content -LiteralPath $stdout -Raw).Contains('probe.usb_accepted')) {
            $accepted=$true; break
        }
        if ($probe.HasExited) { throw "AuthProbe salió antes de aceptar el teléfono: $(Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue)" }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    if (!$accepted) { throw 'AuthProbe no recibió la prueba firmada tras introducir el código correcto.' }
    Write-Output 'PASS: solo el código exacto habilita la prueba firmada y el AuthProbe acepta el teléfono.'

    # Remove only the temporary pairing created by this test while preserving audio/video preferences.
    $forget=$null
    for($scroll=0;$scroll -lt 3 -and !$forget;$scroll++) {
        Invoke-Adb @('shell','input','swipe','540','2100','540','500','350') | Out-Null
        Start-Sleep -Milliseconds 300
        $forgetDoc=(Get-UiDocument)
        $forget=Find-UiNode $forgetDoc { param($node) $node.text -match 'Olvidar' }
    }
    if ($forget) { Tap-UiNode $forget; Start-Sleep -Milliseconds 300 }
    $pairedPreferences=''
    for($check=0;$check -lt 10;$check++) {
        $pairedPreferences=(Invoke-Adb @('shell','run-as','com.streamcam.diagnostics','cat','shared_prefs/paired-pc.xml') -join "`n")
        if($pairedPreferences -notmatch 'name="peer"') {break}
        Start-Sleep -Milliseconds 200
    }
    if ($pairedPreferences -match 'name="peer"') { throw 'El emparejamiento temporal siguió guardado tras pulsar Olvidar PC.' }
    $passed=$true
}
finally {
    if ($probe -and !$probe.HasExited) { Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue }
    if ($reverseAdded) { Invoke-Adb @('reverse','--remove','tcp:47831') | Out-Null }
    $identityPath=[IO.Path]::GetFullPath((Join-Path $temp 'identity'))
    $tempPrefix=[IO.Path]::GetFullPath($temp).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    if ($identityPath.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $identityPath)) {
        Remove-Item -LiteralPath $identityPath -Recurse -Force
    }
    $env:Path=$oldPath
    if ($null -eq $oldUsb) { Remove-Item Env:STREAMCAM_AUTH_USB -ErrorAction SilentlyContinue } else { $env:STREAMCAM_AUTH_USB=$oldUsb }
    if ($null -eq $oldReport) { Remove-Item Env:STREAMCAM_AUTH_REPORT_USB_CODE -ErrorAction SilentlyContinue } else { $env:STREAMCAM_AUTH_REPORT_USB_CODE=$oldReport }
    if ($null -eq $oldIdentityDir) { Remove-Item Env:STREAMCAM_AUTH_IDENTITY_DIR -ErrorAction SilentlyContinue } else { $env:STREAMCAM_AUTH_IDENTITY_DIR=$oldIdentityDir }
    if ($null -eq $oldTemp) { Remove-Item Env:TEMP -ErrorAction SilentlyContinue } else { $env:TEMP=$oldTemp }
    if ($null -eq $oldTmp) { Remove-Item Env:TMP -ErrorAction SilentlyContinue } else { $env:TMP=$oldTmp }
    if ($passed -and (Test-Path -LiteralPath $temp)) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
