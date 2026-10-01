# D:\Work\Fut\tools\work\restore.ps1
# Runs ELEVATED via the ClbqAdmin scheduled task.
# Stops the game and puts the genuine zlibwapi.dll back, verifying the hash.
# ASCII only.
$ErrorActionPreference = 'Continue'

$GAME    = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$TARGET  = Join-Path $GAME 'zlibwapi.dll'
$FORWARD = Join-Path $GAME 'zlibwapi_orig.dll'
$INI     = Join-Path $GAME 'Dumper-7.ini'
$LOG     = 'D:\Work\Fut\evidence\restore.log'
# sha256 of the genuine 2026-09-22 zlibwapi.dll captured before deployment
$EXPECT  = '45B67E07D98CBE8FAEFFBA41BDB1647B2F6BB0A4D9A1C1B3E1F5C2A5D4E7A1B2'

function W([string]$m) {
    $line = "$(Get-Date -Format 'HH:mm:ss') $m"
    $line | Out-File $LOG -Append -Encoding ASCII
    Write-Host $line
}

'' | Out-File $LOG -Encoding ASCII
W "=== restore ==="

$running = Get-Process -Name 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau' -ErrorAction SilentlyContinue
if ($running) {
    W ("stopping: " + (($running | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ' '))
    foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
    Start-Sleep -Seconds 5
}
if (Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue) {
    W "ABORT: Shipping still alive"; exit 2
}

if (-not (Test-Path $FORWARD)) { W "ABORT: forward target missing"; exit 1 }
$fwdHash = (Get-FileHash $FORWARD -Algorithm SHA256).Hash
W "forward target sha256: $fwdHash"

# unlock/overwrite
for ($i = 0; $i -lt 6; $i++) {
    try { $s = [IO.File]::Open($TARGET,'Open','Write','None'); $s.Close(); break }
    catch { Start-Sleep -Seconds 1 }
}
Copy-Item $FORWARD $TARGET -Force
$nowHash = (Get-FileHash $TARGET -Algorithm SHA256).Hash
W "restored sha256      : $nowHash"
if ($nowHash -ne $fwdHash) { W "ABORT: restore hash mismatch"; exit 4 }
W "restore OK (matches forward-target backup)"

Remove-Item $INI -Force -ErrorAction SilentlyContinue
W "removed $INI (if present)"
W "=== restore done ==="
