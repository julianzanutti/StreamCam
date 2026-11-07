$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$files = & git -C $projectRoot ls-files --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'No se pudo enumerar el contenido visible de Git.' }
$forbidden = @('.apk','.aab','.exe','.dll','.pdb','.jks','.keystore','.pfx','.p12','.key','.pem','.log')
$patterns = [ordered]@{
    'clave privada' = '-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/]{64}'
    'token de credencial' = '(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|sk-[A-Za-z0-9]{40,}|AKIA[0-9A-Z]{16})'
    'ruta personal de Windows' = '[A-Za-z]:[\\/]Users[\\/][A-Za-z0-9_.-]+[\\/]'
}
$issues = [System.Collections.Generic.List[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $files) {
    if ([string]::IsNullOrWhiteSpace($relative) -or !$seen.Add($relative)) { continue }
    $path = Join-Path $projectRoot $relative
    if ([IO.Path]::GetExtension($relative).ToLowerInvariant() -in $forbidden) {
        $issues.Add("${relative}: binario generado o archivo privado")
        continue
    }
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    try { $content = [IO.File]::ReadAllText($path) } catch { continue }
    foreach ($rule in $patterns.GetEnumerator()) {
        if ([regex]::IsMatch($content, $rule.Value)) { $issues.Add("${relative}: $($rule.Key)") }
    }
}
if ($issues.Count) {
    $issues | ForEach-Object { Write-Output $_ }
    throw "Falló la auditoría de fuente: $($issues.Count) hallazgos. No se imprimieron valores sensibles."
}
Write-Output "PASS: $($seen.Count) archivos visibles para Git; no se encontraron claves, tokens, rutas personales ni binarios generados."
