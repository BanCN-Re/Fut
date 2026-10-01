# D:\Work\Fut\tools\work\deploy_split.ps1
# Runs ELEVATED via the ClbqAdmin scheduled task.
# Deploys the SPLIT architecture:
#   zlibwapi.dll  <- thin carrier (21 KB, forwards to zlibwapi_orig.dll)
#   zw_core.dll   <- payload (1.1 MB, loaded at runtime by the carrier)
#
# ASCII only (PowerShell 5.1 mangles BOM-less UTF-8 CJK).
$ErrorActionPreference = 'Continue'

$GAME    = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$TARGET  = Join-Path $GAME 'zlibwapi.dll'
$FORWARD = Join-Path $GAME 'zlibwapi_orig.dll'
$CARRIER = 'D:\Work\Fut\cheat\build\loader\zlibwapi.dll'
$CORE    = 'D:\Work\Fut\cheat\build\core\zw_core.dll'
$CORE_DST= Join-Path $GAME 'zw_core.dll'
$LOG     = 'D:\Work\Fut\evidence\deploy_split.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W "=== deploy_split ==="

foreach ($p in @($CARRIER, $CORE)) {
    if (-not (Test-Path $p)) { W "ABORT: missing $p"; exit 1 }
}
$carrierHash = (Get-FileHash $CARRIER -Algorithm SHA256).Hash
$coreHash    = (Get-FileHash $CORE    -Algorithm SHA256).Hash
W ("carrier: {0,10:N0} B  sha={1}" -f (Get-Item $CARRIER).Length, $carrierHash.Substring(0,16))
W ("core   : {0,10:N0} B  sha={1}" -f (Get-Item $CORE).Length,    $coreHash.Substring(0,16))

# stop the game
foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
Start-Sleep -Seconds 5
if (Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue) { W "ABORT: game still running"; exit 2 }
W "game stopped"

# forward target must exist and be the genuine library
if (-not (Test-Path $FORWARD)) { W "ABORT: forward target missing"; exit 3 }
$fwdHash = (Get-FileHash $FORWARD -Algorithm SHA256).Hash
$fb = [IO.File]::ReadAllBytes($FORWARD)
if ([Text.Encoding]::ASCII.GetString($fb) -match 'zlibwapi_orig') { W "ABORT: forward target is itself a proxy"; exit 4 }
W "forward target ok: $($fwdHash.Substring(0,16))"

# deploy
Copy-Item $CARRIER $TARGET   -Force
Copy-Item $CORE    $CORE_DST -Force
$depCarrier = (Get-FileHash $TARGET   -Algorithm SHA256).Hash
$depCore    = (Get-FileHash $CORE_DST -Algorithm SHA256).Hash
if ($depCarrier -ne $carrierHash) { W "ABORT: carrier hash mismatch"; exit 5 }
if ($depCore    -ne $coreHash)    { W "ABORT: core hash mismatch";    exit 6 }
W "deployed carrier -> $TARGET"
W "deployed core    -> $CORE_DST"

# clear stale logs
foreach ($f in 'fut_cheat.trace.log','zw_core.trace.log') {
    $p = Join-Path $GAME $f
    if (Test-Path $p) { Remove-Item $p -Force; W "cleared $f" }
}

Start-Process -FilePath 'D:\CLBQ\CalabiYau\CalabiYau.exe' -WorkingDirectory 'D:\CLBQ\CalabiYau'
W "launcher started"

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 300) {
    $g = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($g) { W "Shipping up: pid=$($g.Id) after $([int]$sw.Elapsed.TotalSeconds)s"; break }
    Start-Sleep -Seconds 2
}
W "=== deploy_split done ==="
