param(
    [Parameter(Mandatory=$true)][string]$ProbePath,
    [Parameter(Mandatory=$true)][string]$IdentityTemplate,
    [Parameter(Mandatory=$true)][string]$WorkRoot
)
$ErrorActionPreference='Stop'
$ProbePath=[IO.Path]::GetFullPath($ProbePath)
$IdentityTemplate=[IO.Path]::GetFullPath($IdentityTemplate)
$WorkRoot=[IO.Path]::GetFullPath($WorkRoot)
$identityInProbe=Join-Path (Split-Path -Parent $ProbePath) 'create-identity.ps1'
if(!(Test-Path -LiteralPath $identityInProbe)){Copy-Item -LiteralPath $IdentityTemplate -Destination $identityInProbe}
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$RunRoot=Join-Path $WorkRoot ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Net.Security;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
public static class StreamCamTlsPin {
    public static string Expected;
    public static bool Validate(object sender, X509Certificate certificate, X509Chain chain, SslPolicyErrors errors) {
        try {
            using (var cert = new X509Certificate2(certificate))
            using (var key = cert.GetRSAPublicKey()) {
                if (key == null) return false;
                var digest = SHA256.HashData(key.ExportSubjectPublicKeyInfo());
                var pin = BitConverter.ToString(digest).Replace("-", "").ToLowerInvariant();
                return String.Equals(pin, Expected, StringComparison.OrdinalIgnoreCase);
            }
        } catch { return false; }
    }
}
'@

function Start-ControlProbe([bool]$V2,[int]$Pairs) {
    $identityName=if($V2){'identity-v2'}else{'identity-v1'}
    $identityDir=Join-Path $RunRoot $identityName
    New-Item -ItemType Directory -Force -Path $identityDir | Out-Null
    $psi=[Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=$ProbePath
    $psi.WorkingDirectory=Split-Path -Parent $ProbePath
    $psi.UseShellExecute=$false
    $psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true
    $psi.RedirectStandardError=$false
    $psi.Environment['STREAMCAM_AUTH_CONTROL_PROBE']='1'
    $psi.Environment['STREAMCAM_AUTH_CONTROL_V2']=if($V2){'1'}else{'0'}
    $psi.Environment['STREAMCAM_AUTH_CONTROL_PAIRS']=[string]$Pairs
    $psi.Environment['STREAMCAM_AUTH_IDENTITY_DIR']=$identityDir
    $psi.Environment['STREAMCAM_AUTH_USB']='1'
    $psi.Environment['STREAMCAM_AUTH_DELAY_MS']='20'
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$psi
    if(!$process.Start()){throw 'No se pudo iniciar AuthProbe.'}
    return [pscustomobject]@{Process=$process;V2=$V2;ReadTask=$null}
}
function Read-ProbeJson($state,[string]$ExpectedType,[int]$TimeoutSeconds=15) {
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while([DateTime]::UtcNow -lt $deadline) {
        if($state.Process.HasExited){throw "AuthProbe terminó inesperadamente con código $($state.Process.ExitCode)."}
        if($null -eq $state.ReadTask){$state.ReadTask=$state.Process.StandardOutput.ReadLineAsync()}
        $read=$state.ReadTask
        if(!$read.Wait(250)){continue}
        $line=$read.Result
        $state.ReadTask=$null
        if($null -eq $line){continue}
        if(!$line.StartsWith('{')){continue}
        $item=$line | ConvertFrom-Json
        if($item.type -eq $ExpectedType){return $item}
    }
    throw "Tiempo agotado esperando $ExpectedType del AuthProbe."
}
function Send-Text($socket,$object) {
    $bytes=[Text.Encoding]::UTF8.GetBytes(($object | ConvertTo-Json -Compress -Depth 8))
    [void]$socket.SendAsync([ArraySegment[byte]]::new($bytes),[Net.WebSockets.WebSocketMessageType]::Text,$true,[Threading.CancellationToken]::None).GetAwaiter().GetResult()
}
function Receive-Text($socket,[int]$TimeoutSeconds=10) {
    $buffer=New-Object byte[] 4096
    $builder=[Text.StringBuilder]::new()
    $cancel=[Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
    try {
        do {
            $result=$socket.ReceiveAsync([ArraySegment[byte]]::new($buffer),$cancel.Token).GetAwaiter().GetResult()
            if($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close){return $null}
            if($result.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text){throw 'Tipo de frame inesperado.'}
            [void]$builder.Append([Text.Encoding]::UTF8.GetString($buffer,0,$result.Count))
        } while(!$result.EndOfMessage)
        return $builder.ToString() | ConvertFrom-Json
    } catch [OperationCanceledException] { throw "Tiempo agotado esperando respuesta del WebSocket ($TimeoutSeconds s)." }
    finally {$cancel.Dispose()}
}
function Connect-PairedClient($invitation) {
    $rsa=[Security.Cryptography.RSA]::Create(2048)
    $public=$rsa.ExportSubjectPublicKeyInfo()
    $id=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($public)).ToLowerInvariant()
    $socket=[Net.WebSockets.ClientWebSocket]::new()
    $socket.Options.Proxy=[Net.WebProxy]::new()
    $socket.Options.Proxy.BypassProxyOnLocal=$true
    $expectedPin=[string]$invitation.pc_key_sha256
    [StreamCamTlsPin]::Expected=$expectedPin
    $socket.Options.RemoteCertificateValidationCallback=[Net.Security.RemoteCertificateValidationCallback]([StreamCamTlsPin]::Validate)
    $socket.Options.KeepAliveInterval=[TimeSpan]::FromSeconds(0)
    try {$socket.ConnectAsync([Uri]$invitation.endpoint,[Threading.CancellationToken]::None).GetAwaiter().GetResult()}
    catch {
        $messages=@();$cause=$_.Exception
        while($cause){$messages+=$cause.GetType().Name+': '+$cause.Message;$cause=$cause.InnerException}
        throw "No se pudo abrir TLS al endpoint anunciado ($($invitation.endpoint)): $($messages -join ' -> ')"
    }
    Send-Text $socket ([ordered]@{version=1;type='usb.hello';device_id=$id;device_key=[Convert]::ToBase64String($public)})
    $challenge=Receive-Text $socket
    if($challenge.type -ne 'usb.challenge' -or $challenge.verification_code -notmatch '^\d{6}$'){throw 'No se recibió un challenge USB válido.'}
    $proofText="StreamCam USB v1`n$($invitation.pc_id)`n$($challenge.challenge)"
    $signature=$rsa.SignData([Text.Encoding]::UTF8.GetBytes($proofText),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    Send-Text $socket ([ordered]@{version=1;type='usb.proof';device_id=$id;device_proof=[Convert]::ToBase64String($signature)})
    $pending=Receive-Text $socket
    if($pending.type -ne 'pair.pending'){throw 'No se recibió pair.pending.'}
    $accepted=Receive-Text $socket
    if($accepted.type -ne 'pair.accepted'){throw 'No se recibió pair.accepted.'}
    return [pscustomobject]@{Socket=$socket;Rsa=$rsa;Identity=$id;Accepted=$accepted}
}
function Close-ControlClient($client) {
    if($client.Socket.State -eq [Net.WebSockets.WebSocketState]::Open) {
        Send-Text $client.Socket @{type='probe.control.close'}
        try {[void](Receive-Text $client.Socket 3)}catch{}
    }
    $client.Socket.Dispose();$client.Rsa.Dispose()
}
$previousProbe=$env:STREAMCAM_AUTH_PROBE
$env:STREAMCAM_AUTH_PROBE=$ProbePath
try {
    # V1 peer: legacy pair.accepted has no control metadata; a v2 hello must be rejected.
    $old=Start-ControlProbe $false 1
    $invite=Read-ProbeJson $old 'probe.invitation'
    $legacy=Connect-PairedClient $invite.payload
    if($legacy.Accepted.PSObject.Properties.Name -contains 'control_version'){throw 'El pairing v1 anunció control v2 sin negociación.'}
    Send-Text $legacy.Socket @{type='control.hello';version=2;capabilities=@('audio.effects.ack')}
    if($null -ne (Receive-Text $legacy.Socket 5)){throw 'Un servidor v1 aceptó control.hello no solicitado.'}
    $legacy.Socket.Dispose();$legacy.Rsa.Dispose()
    if(!$old.Process.WaitForExit(10000)){throw 'El AuthProbe v1 no terminó después del mensaje de cierre esperado.'}
    if($old.Process.ExitCode -ne 0){throw "El AuthProbe v1 terminó con código $($old.Process.ExitCode)."}

    # V2 peer: negotiate the shared capability and retry the same request ID.
    $new=Start-ControlProbe $true 1
    $invite=Read-ProbeJson $new 'probe.invitation'
    $client=Connect-PairedClient $invite.payload
    if($client.Accepted.PSObject.Properties.Name -contains 'control_version'){throw 'pair.accepted volvió a anunciar extensiones optativas.'}
    Send-Text $client.Socket @{type='control.hello';version=2;capabilities=@('audio.effects.ack')}
    $ready=Receive-Text $client.Socket
    if($ready.type -ne 'control.ready' -or $ready.version -ne 2 -or $ready.capabilities -notcontains 'audio.effects.ack'){throw 'No se negoció audio.effects.ack.'}
    $requestId=[Guid]::NewGuid().ToString('N')
    $command=@{type='audio.effects';version=2;request_id=$requestId;echoCancellation=$false}
    Send-Text $client.Socket $command
    $discardedAck=Receive-Text $client.Socket
    if($discardedAck.type -ne 'audio.effects.ack' -or $discardedAck.request_id -ne $requestId -or !$discardedAck.ok){throw 'ACK inicial de audio.effects inválido.'}
    # Drop the first ACK at the test client, then retry the identical command and request ID.
    Send-Text $client.Socket $command
    $retryAck=Receive-Text $client.Socket
    if($retryAck.type -ne 'audio.effects.ack' -or $retryAck.request_id -ne $requestId -or !$retryAck.ok){throw 'Retry idempotente no recibió ACK.'}
    Close-ControlClient $client
    if(!$new.Process.WaitForExit(10000) -or $new.Process.ExitCode -ne 0){throw 'El AuthProbe V2 no terminó correctamente.'}

    # A peer with no shared capability must not accept AEC commands.
    $emptyServer=Start-ControlProbe $true 1
    $invite=Read-ProbeJson $emptyServer 'probe.invitation'
    $empty=Connect-PairedClient $invite.payload
    Send-Text $empty.Socket @{type='control.hello';version=2;capabilities=@()}
    $ready=Receive-Text $empty.Socket
    if($ready.type -ne 'control.ready' -or $ready.capabilities.Count -ne 0){throw 'No se preservó la intersección vacía de capacidades.'}
    Send-Text $empty.Socket @{type='audio.effects';version=2;request_id=[Guid]::NewGuid().ToString('N');echoCancellation=$true}
    if($null -ne (Receive-Text $empty.Socket 5)){throw 'El servidor permitió audio.effects sin capacidad negociada.'}
    $empty.Socket.Dispose();$empty.Rsa.Dispose()
    if(!$emptyServer.Process.WaitForExit(10000) -or $emptyServer.Process.ExitCode -ne 0){throw 'El AuthProbe de capacidades vacías no terminó correctamente.'}
    Write-Output 'PASS: WebSocket TLS loopback, pairing legado/v2, capabilities, ACK y retry con mismo request_id.'
} finally {
    foreach($state in @($old,$new,$emptyServer)) {
        if($null -ne $state) {
            if(!$state.Process.HasExited){$state.Process.Kill();[void]$state.Process.WaitForExit(5000)}
            $state.Process.Dispose()
        }
    }
    $env:STREAMCAM_AUTH_PROBE=$previousProbe
    $safeRunRoot=[IO.Path]::GetFullPath($RunRoot)
    $safeWorkRoot=[IO.Path]::GetFullPath($WorkRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    if($safeRunRoot.StartsWith($safeWorkRoot,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $safeRunRoot)) {
        Remove-Item -LiteralPath $safeRunRoot -Recurse -Force
    }
}
