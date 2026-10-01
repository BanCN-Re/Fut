# D:\Work\Fut\tools\work\deploy_and_launch.ps1
# Runs ELEVATED via the ClbqAdmin scheduled task.
# 1) stop the game  2) back up current zlibwapi.dll -> zlibwapi_orig.dll
# 3) deploy the Dumper-7 proxy as zlibwapi.dll  4) write Dumper-7.ini
# 5) launch the official launcher  6) wait for the Shipping process
# All ASCII only (PowerShell 5.1 chokes on BOM-less UTF-8 CJK).
$ErrorActionPreference = 'Continue'

$GAME   = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$TARGET = Join-Path $GAME 'zlibwapi.dll'
$FORWARD= Join-Path $GAME 'zlibwapi_orig.dll'
$PROXY  = 'D:\Work\Fut\dumper\Dumper-7.proxy.dll'
$INI    = Join-Path $GAME 'Dumper-7.ini'
$LOG    = 'D:\Work\Fut\evidence\deploy_launch.log'

function W([string]$m) {
    $line = "$(Get-Date -Format 'HH:mm:ss') $m"
    $line | Out-File $LOG -Append -Encoding ASCII
    Write-Host $line
}

'' | Out-File $LOG -Encoding ASCII
W "=== deploy_and_launch ==="
W "proxy source : $PROXY"

# --- 0. sanity on inputs -------------------------------------------------
if (-not (Test-Path $PROXY)) { W "ABORT: proxy missing"; exit 1 }
$proxyHash = (Get-FileHash $PROXY -Algorithm SHA256).Hash
W "proxy sha256 : $proxyHash"

# --- 1. stop game --------------------------------------------------------
$names = 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau'
$running = Get-Process -Name $names -ErrorAction SilentlyContinue
if ($running) {
    W ("stopping: " + (($running | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ' '))
    foreach ($n in $names) { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
    Start-Sleep -Seconds 5
} else {
    W "no game process running"
}
$still = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
if ($still) { W "ABORT: Shipping still alive pid=$($still.Id)"; exit 2 }
W "game stopped"

# --- 2. back up the genuine zlib as the forward target -------------------
$curHash = (Get-FileHash $TARGET -Algorithm SHA256).Hash
W "current zlibwapi.dll sha256 : $curHash"
Copy-Item $TARGET $FORWARD -Force
$fwdHash = (Get-FileHash $FORWARD -Algorithm SHA256).Hash
if ($fwdHash -ne $curHash) { W "ABORT: forward-target backup hash mismatch"; exit 3 }
W "forward target written  : $FORWARD ($fwdHash)"

# --- 3. deploy proxy -----------------------------------------------------
Copy-Item $PROXY $TARGET -Force
$depHash = (Get-FileHash $TARGET -Algorithm SHA256).Hash
if ($depHash -ne $proxyHash) { W "ABORT: deployed hash mismatch ($depHash)"; exit 4 }
W "deployed proxy          : $TARGET ($depHash)"

# --- 4. Dumper-7.ini in the game dir (takes precedence over C:\Dumper-7) --
@"
[Settings]
SDKGenerationPath=D:/Work/Fut/sdk
SleepTimeout=180
SDKNamespaceName=SDK
"@ | Out-File $INI -Encoding ASCII
W "wrote $INI"
Get-Content $INI | ForEach-Object { W "  ini| $_" }

# --- 5. launch -----------------------------------------------------------
Start-Process -FilePath 'D:\CLBQ\CalabiYau\CalabiYau.exe' -WorkingDirectory 'D:\CLBQ\CalabiYau'
W "launcher started"

# --- 6. wait for the game ------------------------------------------------
$sw = [Diagnostics.Stopwatch]::StartNew()
$pid_ = $null
while ($sw.Elapsed.TotalSeconds -lt 300) {
    $g = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($g) { $pid_ = $g.Id; break }
    Start-Sleep -Seconds 2
}
if ($pid_) {
    W "Shipping up: pid=$pid_ after $([int]$sw.Elapsed.TotalSeconds)s"
} else {
    W "WARN: Shipping did not appear within 300s"
}
W "=== deploy_and_launch done ==="
