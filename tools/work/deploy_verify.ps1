# D:\Work\Fut\tools\work\deploy_verify.ps1
# Runs ELEVATED. Deploys the in-process verifier proxy and relaunches the game.
# ASCII only.
$ErrorActionPreference = 'Continue'
$GAME    = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$TARGET  = Join-Path $GAME 'zlibwapi.dll'
$FORWARD = Join-Path $GAME 'zlibwapi_orig.dll'
$PROXY   = 'D:\Work\Fut\dumper\verify\x64\Release\zlibwapi.dll'
$INI     = Join-Path $GAME 'Dumper-7.ini'
$LOG     = 'D:\Work\Fut\evidence\deploy_verify.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W "=== deploy_verify ==="

if (-not (Test-Path $PROXY)) { W "ABORT: verifier proxy missing"; exit 1 }
$proxyHash = (Get-FileHash $PROXY -Algorithm SHA256).Hash
W "verifier sha256: $proxyHash"

# stop game
foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
Start-Sleep -Seconds 5
if (Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue) { W "ABORT: still running"; exit 2 }
W "game stopped"

# ensure the genuine zlib is preserved as the forward target
if (-not (Test-Path $FORWARD)) { W "ABORT: forward target missing"; exit 1 }
$fwdHash = (Get-FileHash $FORWARD -Algorithm SHA256).Hash
W "forward target : $fwdHash"

Copy-Item $PROXY $TARGET -Force
$depHash = (Get-FileHash $TARGET -Algorithm SHA256).Hash
if ($depHash -ne $proxyHash) { W "ABORT: deployed hash mismatch"; exit 3 }
W "deployed verifier: $depHash"

# remove the Dumper-7 ini so it does not re-dump on this boot
if (Test-Path $INI) { Remove-Item $INI -Force; W "removed Dumper-7.ini (no re-dump)" }

Start-Process -FilePath 'D:\CLBQ\CalabiYau\CalabiYau.exe' -WorkingDirectory 'D:\CLBQ\CalabiYau'
W "launcher started"

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 300) {
    $g = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($g) { W "Shipping up: pid=$($g.Id) after $([int]$sw.Elapsed.TotalSeconds)s"; break }
    Start-Sleep -Seconds 2
}
W "=== deploy_verify done ==="
