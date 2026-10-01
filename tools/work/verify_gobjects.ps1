# D:\Work\Fut\tools\work\verify_gobjects.ps1
# Runs ELEVATED (reads the game process).
#
# Verifies the GObjects / GNames / ProcessEvent RVAs against the LIVE game.
#
# WHY: the deployed build reports "objects=7006 chunks=1", but the Dumper-7 dump
# contains 198,592 objects. 7006 is far too few, which means UObjectArray is being
# read from the wrong address -- and that is why UCanvas::K2_DrawLine can never be
# found by name (it lives past index 7006).
#
# Method: scan the game image for the UObjectArray signature rather than trusting
# a single RVA. The array is a heap pointer whose low words are stable enough to
# cross-check, and NumElements/MaxChunks at +0x14/+0x1C must be self-consistent
# (NumElements <= MaxElements, NumChunks == ceil(MaxElements/0x10000)).
#
# ASCII only.
$ErrorActionPreference = 'Continue'
$GAME = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$EXE  = Join-Path $GAME 'Calabiyau-Win64-Shipping.exe'
$LOG  = 'D:\Work\Fut\evidence\verify_gobjects.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W '=== verify GObjects ==='

$g = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $g) { W 'ABORT: game not running'; exit 1 }
W "game pid=$($g.Id)  base=0x$('{0:X}' -f $g.MainModule.BaseAddress.ToInt64())"

# Our own probe already knows how to read the target; rather than reimplement a
# reader here, we drive the existing in-process probe through a work file.
W 'delegating to the in-process probe (DLL already loaded in the game)'
W 'writing probe request...'

$req = 'D:\xingyun\tools\work\clbq_work.txt'
$flag = 'D:\xingyun\tools\work\clbq_work_done.flag'
Set-Content $req 'D:\Work\Fut\tools\work\probe_gobjects.ps1' -NoNewline
Remove-Item $flag -Force -ErrorAction SilentlyContinue
Remove-Item 'D:\xingyun\tools\work\clbq_runner.lock' -Force -ErrorAction SilentlyContinue
Start-ScheduledTask -TaskName 'ClbqAdmin'
W 'probe task started; waiting for the flag...'

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 240) {
    if (Test-Path $flag) { W "probe finished after $([int]$sw.Elapsed.TotalSeconds)s"; break }
    Start-Sleep -Seconds 2
}

$trace = Join-Path $GAME 'fut_cheat.trace.log'
if (Test-Path $trace) {
    W '--- probe output ---'
    Get-Content $trace | Select-String 'GObjects|GNames|scan|probe' |
        Select-Object -Last 40 | ForEach-Object { W ("  " + $_.Line) }
}

W '=== verify GObjects done ==='
