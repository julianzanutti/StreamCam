param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security.Cryptography.ProtectedData
[IO.Directory]::CreateDirectory($Directory) | Out-Null
$certPath = Join-Path $Directory 'pc-cert.pem'
$keyPath = Join-Path $Directory 'pc-key.dpapi'
$entropy = [Text.Encoding]::UTF8.GetBytes('StreamCam identity v1')
if (!(Test-Path -LiteralPath $certPath) -or !(Test-Path -LiteralPath $keyPath)) {
    $rsa = [Security.Cryptography.RSA]::Create(2048)
    try {
        $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
            'CN=StreamCam', $rsa, [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.RSASignaturePadding]::Pkcs1)
        $certificate = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddYears(5))
        $key = $rsa.ExportPkcs8PrivateKey()
        try {
            $protected = [Security.Cryptography.ProtectedData]::Protect($key, $entropy, [Security.Cryptography.DataProtectionScope]::CurrentUser)
            [IO.File]::WriteAllBytes($keyPath, $protected)
            [IO.File]::WriteAllText($certPath, $certificate.ExportCertificatePem())
        } finally { [Array]::Clear($key, 0, $key.Length) }
    } finally { $rsa.Dispose() }
}
# Return private material only over the parent process pipe, never in files or logs.
$raw = [Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($keyPath), $entropy, [Security.Cryptography.DataProtectionScope]::CurrentUser)
try {
    $loaded = [Security.Cryptography.RSA]::Create()
    try {
        $readCount = 0
        $loaded.ImportPkcs8PrivateKey($raw, [ref]$readCount)
        $pin = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($loaded.ExportSubjectPublicKeyInfo())).ToLowerInvariant()
    } finally { $loaded.Dispose() }
    $result = @{ certificate = [IO.File]::ReadAllText($certPath); key = [Convert]::ToBase64String($raw); pin = $pin }
    [Console]::Out.Write(($result | ConvertTo-Json -Compress))
} finally { [Array]::Clear($raw, 0, $raw.Length) }
