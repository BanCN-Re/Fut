# D:\Work\Fut\tools\work\deploy_cheat.ps1
# Runs ELEVATED via the ClbqAdmin task.
# Stops the game, backs up the genuine zlib as the forward target, deploys the
# cheat proxy, and relaunches. ASCII only.
$ErrorActionPreference = 'Continue'
$GAME    = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$TARGET  = Join-Path $GAME 'zlibwapi.dll'
$FORWARD = Join-Path $GAME 'zlibwapi_orig.dll'
$PROXY   = 'D:\Work\Fut\cheat\build\zlibwapi.dll'
$LOG     = 'D:\Work\Fut\evidence\deploy_cheat.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W "=== deploy_cheat ==="

if (-not (Test-Path $PROXY)) { W "ABORT: proxy missing: $PROXY"; exit 1 }
$proxyHash = (Get-FileHash $PROXY -Algorithm SHA256).Hash
$proxySize = (Get-Item $PROXY).Length
W "proxy: $proxySize B  sha256=$proxyHash"

foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
Start-Sleep -Seconds 5
if (Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue) { W "ABORT: game still running"; exit 2 }
W "game stopped"

# The genuine zlib must be the forward target. Prefer the known-good backup.
if (-not (Test-Path $FORWARD)) {
    W "ABORT: forward target missing: $FORWARD"
    exit 3
}
$fwdHash = (Get-FileHash $FORWARD -Algorithm SHA256).Hash
$fwdSize = (Get-Item $FORWARD).Length
W "forward target: $fwdSize B  sha256=$fwdHash"

# sanity: the forward target must be the genuine zlib (no self-forwarding)
$fb = [IO.File]::ReadAllBytes($FORWARD)
$fs = [Text.Encoding]::ASCII.GetString($fb)
if ($fs -match 'zlibwapi_orig') { W "ABORT: forward target is itself a proxy (would recurse)"; exit 4 }
W "forward target verified genuine"

Copy-Item $PROXY $TARGET -Force
$depHash = (Get-FileHash $TARGET -Algorithm SHA256).Hash
if ($depHash -ne $proxyHash) { W "ABORT: deployed hash mismatch"; exit 5 }
W "deployed: $TARGET"

# remove any leftover Dumper-7 ini so it does not re-dump on this boot
$ini = Join-Path $GAME 'Dumper-7.ini'
if (Test-Path $ini) { Remove-Item $ini -Force; W "removed Dumper-7.ini" }

# clear old logs so the new session is unambiguous
foreach ($f in 'fut_cheat.trace.log','fut_verify_report.txt','fut_probe_report.txt') {
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
W "=== deploy_cheat done ==="
