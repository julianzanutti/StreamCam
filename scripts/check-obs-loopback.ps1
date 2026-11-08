param(
    [Parameter(Mandatory = $true)][string]$ObsRoot,
    [Parameter(Mandatory = $true)][string]$PluginDll,
    [Parameter(Mandatory = $true)][string]$SyntheticMediaProbe
)

$ErrorActionPreference = 'Stop'
$ObsRoot = [IO.Path]::GetFullPath($ObsRoot)
$PluginDll = [IO.Path]::GetFullPath($PluginDll)
$SyntheticMediaProbe = [IO.Path]::GetFullPath($SyntheticMediaProbe)
$obsExe = Join-Path $ObsRoot 'bin/64bit/obs64.exe'
$pluginDirectory = Join-Path $ObsRoot 'obs-plugins/64bit'
$pluginData = Join-Path $ObsRoot 'data/obs-plugins/streamcam-source'
$configPath = Join-Path $ObsRoot 'config/obs-studio/plugin_config/obs-websocket/config.json'
$logDirectory = Join-Path $ObsRoot 'config/obs-studio/logs'
$testName = 'StreamCam loopback QA ' + [guid]::NewGuid().ToString('N')
$serverPort = 4458
$obs = $null
$publisher = $null
$socket = $null
$sourceCreated = $false
$previousPath = $env:Path
$projectRoot = Split-Path $PSScriptRoot -Parent
$qtBin = Join-Path $projectRoot '.tooling/Qt/6.8.3/msvc2022_64/bin'

foreach ($file in @($obsExe, $PluginDll, $SyntheticMediaProbe)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required loopback test file is missing: $file" }
}
New-Item -ItemType Directory -Force $pluginDirectory,$pluginData | Out-Null
Copy-Item -LiteralPath $PluginDll -Destination (Join-Path $pluginDirectory 'streamcam-source.dll') -Force

if (!(Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "OBS WebSocket configuration was not initialized: $configPath"
}
$websocketConfig = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$websocketConfig.server_enabled = $true
$websocketConfig.auth_required = $false
$websocketConfig.server_port = $serverPort
$websocketConfig | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $configPath -Encoding utf8

function Receive-ObsMessage([System.Net.WebSockets.ClientWebSocket]$Client,[int]$TimeoutSeconds = 8) {
    $buffer = [byte[]]::new(8192)
    $message = [IO.MemoryStream]::new()
    $cancellation = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
    try {
        do {
            $segment = [ArraySegment[byte]]::new($buffer)
            $result = $Client.ReceiveAsync($segment,$cancellation.Token).GetAwaiter().GetResult()
            if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw 'OBS closed its WebSocket control connection.'
            }
            $message.Write($buffer,0,$result.Count)
        } while (!$result.EndOfMessage)
        return [Text.Encoding]::UTF8.GetString($message.ToArray()) | ConvertFrom-Json
    } finally {
        $cancellation.Dispose()
        $message.Dispose()
    }
}

function Send-ObsMessage([System.Net.WebSockets.ClientWebSocket]$Client,[object]$Value) {
    $json = $Value | ConvertTo-Json -Depth 16 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $segment = [ArraySegment[byte]]::new($bytes)
    $null = $Client.SendAsync($segment,[System.Net.WebSockets.WebSocketMessageType]::Text,$true,[Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Send-ObsRequest([System.Net.WebSockets.ClientWebSocket]$Client,[string]$RequestType,[hashtable]$RequestData = @{}) {
    $requestId = [guid]::NewGuid().ToString('D')
    Send-ObsMessage $Client @{ op = 6; d = @{ requestType = $RequestType; requestId = $requestId; requestData = $RequestData } }
    for ($attempt=0; $attempt -lt 4; $attempt++) {
        $response = Receive-ObsMessage $Client 8
        if ($response.op -ne 7 -or $response.d.requestId -ne $requestId) { continue }
        if (!$response.d.requestStatus.result) {
            throw "OBS WebSocket $RequestType failed: $($response.d.requestStatus.comment)"
        }
        return $response.d.responseData
    }
    throw "OBS did not return a response for $RequestType."
}

function Close-TestObs([System.Diagnostics.Process]$Process) {
    if (!$Process -or $Process.HasExited) { return $true }
    $Process.Refresh()
    if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
        $null = $Process.CloseMainWindow()
    }
    if ($Process.WaitForExit(12000)) { return $true }
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    $Process.WaitForExit(5000) | Out-Null
    return $false
}

try {
    $env:Path = "$qtBin;$env:Path"
    $obs = Start-Process -FilePath $obsExe -ArgumentList @('--portable','--multi','--verbose','--unfiltered_log') `
        -WorkingDirectory (Split-Path $obsExe) -WindowStyle Hidden -PassThru

    $hello = $null
    for ($attempt=0; $attempt -lt 90 -and !$hello; $attempt++) {
        if ($obs.HasExited) { throw "OBS exited during startup with code $($obs.ExitCode)." }
        $candidate = [System.Net.WebSockets.ClientWebSocket]::new()
        try {
            $connectTimeout = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(2))
            try { $candidate.ConnectAsync([Uri]"ws://127.0.0.1:$serverPort",$connectTimeout.Token).GetAwaiter().GetResult() }
            finally { $connectTimeout.Dispose() }
            if ($candidate.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
                $socket = $candidate
                $hello = Receive-ObsMessage $socket 8
            } else { $candidate.Dispose() }
        } catch { $candidate.Dispose() }
        if (!$hello) { Start-Sleep -Milliseconds 250 }
    }
    if (!$hello -or $hello.op -ne 0) { throw 'OBS WebSocket did not send its Hello message.' }

    Send-ObsMessage $socket @{ op = 1; d = @{ rpcVersion = $hello.d.rpcVersion; eventSubscriptions = 0 } }
    $identified = Receive-ObsMessage $socket 8
    if ($identified.op -ne 2) { throw 'OBS WebSocket did not identify the loopback client.' }

    $existing = Send-ObsRequest $socket 'GetInputList'
    foreach ($input in @($existing.inputs | Where-Object { $_.inputName -like 'StreamCam loopback QA*' })) {
        Send-ObsRequest $socket 'RemoveInput' @{ inputName = $input.inputName } | Out-Null
    }
    Start-Sleep -Milliseconds 500
    Send-ObsRequest $socket 'CreateInput' @{
        sceneName = 'Escena'
        inputName = $testName
        inputKind = 'streamcam_source_v1'
        inputSettings = @{}
        sceneItemEnabled = $true
    } | Out-Null
    $sourceCreated = $true

    $publisher = Start-Process -FilePath $SyntheticMediaProbe -ArgumentList @('StreamCamFrames-Loopback-QA','StreamCamAudio-Loopback-QA') `
        -WorkingDirectory (Split-Path $SyntheticMediaProbe) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $ObsRoot 'streamcam-loopback-probe.out.log') `
        -RedirectStandardError (Join-Path $ObsRoot 'streamcam-loopback-probe.err.log')

    $videoReceived = $false
    $audioReceived = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    while ([DateTime]::UtcNow -lt $deadline -and (!$videoReceived -or !$audioReceived)) {
        if ($obs.HasExited) { throw "OBS exited during media loopback with code $($obs.ExitCode)." }
        $log = Get-ChildItem -LiteralPath $logDirectory -Filter '*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($log) {
            $text = Get-Content -LiteralPath $log.FullName -Raw
            $videoReceived = $text.Contains('[StreamCam] First video frame received: 64 x 48')
            $audioReceived = $text.Contains('[StreamCam] First audio packet received: 48000 Hz, 1 channel(s), 960 frames')
        }
        Start-Sleep -Milliseconds 250
    }
    if (!$videoReceived -or !$audioReceived) {
        $tail = if ($log) { (Get-Content -LiteralPath $log.FullName -Tail 30) -join "`n" } else { 'No OBS log was created.' }
        throw "OBS did not receive both synthetic media streams. Video=$videoReceived Audio=$audioReceived`n$tail"
    }
    if (!$publisher.HasExited) { $publisher.WaitForExit(9000) | Out-Null }
    if (!$publisher.HasExited -or $publisher.ExitCode -ne 0) { throw 'The synthetic media publisher did not finish cleanly.' }

    Send-ObsRequest $socket 'RemoveInput' @{ inputName = $testName } | Out-Null
    $sourceCreated = $false
    $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,'test complete',[Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $socket.Dispose()
    $socket = $null
    $cleanShutdown = Close-TestObs $obs

    $publisherLog = Get-Content -LiteralPath (Join-Path $ObsRoot 'streamcam-loopback-probe.out.log') -Raw
    if ($publisherLog -notmatch 'Published synthetic OBS input: \d+ video frames, \d+ audio packets; delivered \d+/\d+ video and \d+/\d+ audio; connected clients \d+/\d+\.') {
        throw 'Synthetic publisher output did not confirm both frame and audio packet production.'
    }
    Write-Output 'PASS: OBS 33.0.1 loaded StreamCam, created its source, and received synthetic video and audio.'
    Write-Output $publisherLog.Trim()
    if (!$cleanShutdown) { Write-Output 'OBS required a forced close after source removal; no recording or stream was active.' }
    Write-Output "OBS log: $($log.FullName)"
} finally {
    if ($publisher -and !$publisher.HasExited) { Stop-Process -Id $publisher.Id -Force -ErrorAction SilentlyContinue }
    if ($socket) {
        try {
            if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
                if ($sourceCreated) { Send-ObsRequest $socket 'RemoveInput' @{ inputName = $testName } | Out-Null }
            }
        } catch { }
        $socket.Dispose()
    }
    if ($obs -and !$obs.HasExited) { Close-TestObs $obs | Out-Null }
    $env:Path = $previousPath
}
