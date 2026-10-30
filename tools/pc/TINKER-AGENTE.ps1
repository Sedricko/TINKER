# Agente do Tinker no PC: a cada versao nova em Releases, instala no emulador (emulator-5554, com Magisk e adb root),
# roda o autoteste e mostra o resultado (e manda para a planilha, se houver link). Deixe a janela aberta; Ctrl+C para sair.
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$env:ANDROID_SERIAL = "emulator-5554"  # so o emulador: o celular no cabo nao e tocado
$app = "com.sedricko.tinker"
$repo = "Sedricko/TINKER"
$done = "$env:USERPROFILE\tinker-novo-testado.txt"   # ultima versao testada
$cfg = "$env:USERPROFILE\tinker-novo-planilha.txt"   # link da planilha (fica so neste PC; vazio = sem planilha)
$log = "$env:USERPROFILE\tinker-novo-log.txt"
function Say($t) { $l = "$(Get-Date -Format 'HH:mm:ss') $t"; Write-Host $l; Add-Content $log $l }

function Post-Sheet($motivo, $problemas, $texto, $versao) {
    $link = (Get-Content $cfg -ErrorAction SilentlyContinue | Select-Object -First 1)
    if (-not $link) { return }
    $url = $link.Trim() + $(if ($link.Contains("?")) { "&" } else { "?" }) + "relatorio=1"
    $body = @{ jogo = "alvo de teste (emulador)"; versao = $versao; aparelho = "Emulador PC"; motivo = $motivo
               problemas = $problemas; texto = $texto } | ConvertTo-Json -Compress
    try { Invoke-WebRequest $url -Method Post -Body ([Text.Encoding]::UTF8.GetBytes($body)) -ContentType "application/json; charset=utf-8" -UseBasicParsing | Out-Null; Say "enviado para a planilha" }
    catch { Say "planilha fora do ar: $($_.Exception.Message)" }
}

function Test-Version {
    $rel = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" -Headers @{ "User-Agent" = "Tinker" }
    $v = $rel.tag_name.TrimStart("v")
    if ((Get-Content $done -ErrorAction SilentlyContinue) -eq $v) { return }
    Say "=== Tinker $v ==="
    $apk = "$env:TEMP\Tinker-novo.apk"
    Invoke-WebRequest ($rel.assets | Where-Object name -like "Tinker-*.apk" | Select-Object -First 1).browser_download_url -OutFile $apk -UseBasicParsing
    Say ("instalado: " + ((& $adb install -r $apk) -join " "))
    # root sem tocar em Permitir: politica do Magisk para o uid do app
    $uid = ((& $adb shell dumpsys package $app) | Select-String "(?:userId|appId)=(\d+)" | Select-Object -First 1).Matches.Groups[1].Value
    if ($uid) { & $adb shell "magisk --sqlite 'REPLACE INTO policies (uid,policy,until,logging,notification) VALUES($uid,2,0,1,1)'" | Out-Null }
    & $adb shell rm -f /data/data/$app/files/autoteste.txt
    & $adb shell am force-stop $app
    & $adb shell am start -n "$app/.MainActivity" --ez autoteste true | Out-Null
    $out = $null
    for ($k = 0; $k -lt 24 -and -not $out; $k++) {
        Start-Sleep 5
        $t = & $adb shell cat /data/data/$app/files/autoteste.txt 2>$null
        if ($LASTEXITCODE -eq 0 -and $t) { $out = $t -join "`n" }
    }
    if (-not $out) { $out = "FALHA o autoteste nao terminou em 2 min" }
    Say $out
    Post-Sheet "agente PC: $(($out -split "`n")[0])" ((($out -split "`n") | Where-Object { $_ -like "FALHA*" }) -join "`n") $out $v
    Set-Content $done $v
}

Say "agente do Tinker ligado (emulator-5554)."
while ($true) {
    try {
        if (-not ((& $adb devices) -match "emulator-5554\s+device")) { Say "abra o emulador do Android Studio (com Magisk)"; Start-Sleep 30; continue }
        & $adb root | Out-Null
        Test-Version
    } catch { Say "erro: $($_.Exception.Message)" }
    Start-Sleep 30
}
