# D:\Work\Fut\tools\work\deploy_core.ps1
# Runs ELEVATED. Deploys the SPLIT architecture:
#   zlibwapi.dll  = thin carrier (~21 KB, forwards the 129 zlib exports)
#   zw_core.dll   = real payload (ESP / aim / skill / overlay / net filter)
#
# The old single-file deploy script copied the monolith over zlibwapi.dll, which
# is NOT this architecture and left the core stale. ASCII only.
$ErrorActionPreference = 'Continue'
$GAME    = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$LOADER  = 'D:\Work\Fut\cheat\build\loader\zlibwapi.dll'
$CORE    = 'D:\Work\Fut\cheat\build\core\zw_core.dll'
$TARGET  = Join-Path $GAME 'zlibwapi.dll'
$COREDST = Join-Path $GAME 'zw_core.dll'
$FORWARD = Join-Path $GAME 'zlibwapi_orig.dll'
$LOG     = 'D:\Work\Fut\evidence\deploy_core.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W "=== deploy_core ==="

foreach ($p in @($LOADER, $CORE)) {
    if (-not (Test-Path $p)) { W "ABORT: missing $p"; exit 1 }
}
$lh = (Get-FileHash $LOADER -Algorithm SHA256).Hash
$ch = (Get-FileHash $CORE   -Algorithm SHA256).Hash
W "loader: $((Get-Item $LOADER).Length) B  sha=$($lh.Substring(0,16))"
W "core  : $((Get-Item $CORE).Length) B  sha=$($ch.Substring(0,16))"

foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
Start-Sleep -Seconds 5
if (Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue) { W "ABORT: game still running"; exit 2 }
W "game stopped"

if (-not (Test-Path $FORWARD)) { W "ABORT: forward target missing"; exit 3 }
$fb = [IO.File]::ReadAllBytes($FORWARD)
if ([Text.Encoding]::ASCII.GetString($fb) -match 'zlibwapi_orig') { W "ABORT: forward target is a proxy"; exit 4 }
W "forward target OK ($((Get-Item $FORWARD).Length) B)"

Copy-Item $LOADER $TARGET  -Force
Copy-Item $CORE   $COREDST -Force
if ((Get-FileHash $TARGET  -Algorithm SHA256).Hash -ne $lh) { W "ABORT: loader hash mismatch";  exit 5 }
if ((Get-FileHash $COREDST -Algorithm SHA256).Hash -ne $ch) { W "ABORT: core hash mismatch";    exit 6 }
W "deployed loader -> $TARGET"
W "deployed core   -> $COREDST"

$ini = Join-Path $GAME 'Dumper-7.ini'
if (Test-Path $ini) { Remove-Item $ini -Force; W "removed Dumper-7.ini" }
foreach ($f in 'fut_cheat.trace.log') {
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
W "=== deploy_core done ==="
