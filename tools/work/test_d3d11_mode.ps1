# D:\Work\Fut\tools\work\test_d3d11_mode.ps1
# Runs ELEVATED via the ClbqAdmin task.
#
# Determines whether the game can be launched in D3D11 mode, which would let us
# draw INSIDE the game's own frame instead of in a separate overlay window.
#
# WHY THIS MATTERS
#   The overlay window is composited by DWM after the game's frame, so it always
#   trails by at least one compositor tick. With the game at 239 fps and a
#   DISCARD swap chain capped at the monitor refresh, boxes could be ~4 frames
#   late. Drawing inside the game's own swap chain removes that entirely.
#
#   The obstacle is that the game presents through D3D12, whose swap chain is a
#   different DXGI object with different Present addresses -- hooking the D3D11
#   vtable never fires (measured: "Present #1" then silence).
#
#   UE4 accepts -d3d11 on the command line. If the game honours it, the existing
#   D3D11 Present hook becomes viable and the overlay problem disappears.
#
# This script only LAUNCHES with the flag and inspects which API loaded.
# ASCII only.
$ErrorActionPreference = 'Continue'
$GAME = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64'
$LOG  = 'D:\Work\Fut\evidence\d3d11_mode_test.log'

function W([string]$m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; $l | Out-File $LOG -Append -Encoding ASCII; Write-Host $l }
'' | Out-File $LOG -Encoding ASCII
W '=== d3d11 mode test ==='

foreach ($n in 'Calabiyau-Win64-Shipping','Calabiyau','CalabiYau') { taskkill /IM "$n.exe" /F /T 2>&1 | Out-Null }
Start-Sleep -Seconds 5
W 'game stopped'

# Launch the game EXE directly with -d3d11. The launcher chain normally passes
# SDK args, but for an API test the exe alone is enough -- it will reach the
# login/menu and we only need to see which graphics DLL loads.
$exe = Join-Path $GAME 'Calabiyau-Win64-Shipping.exe'
W "launching: $exe -d3d11"
Start-Process -FilePath $exe -ArgumentList '-d3d11' -WorkingDirectory $GAME

$sw = [Diagnostics.Stopwatch]::StartNew()
$pid_ = $null
while ($sw.Elapsed.TotalSeconds -lt 180) {
    $g = Get-Process -Name 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($g) { $pid_ = $g.Id; break }
    Start-Sleep -Seconds 2
}
if (-not $pid_) { W 'ABORT: game did not start'; exit 1 }
W "game pid=$pid_ after $([int]$sw.Elapsed.TotalSeconds)s"

# Give it time to create the device, then read the trace which lists the
# graphics modules the probe found.
W 'waiting 60s for engine + device creation...'
Start-Sleep -Seconds 60

$trace = Join-Path $GAME 'fut_cheat.trace.log'
if (Test-Path $trace) {
    W '--- graphics summary from the in-process probe ---'
    Get-Content $trace | Select-String 'SUMMARY|d3d11=|d3d12=' | ForEach-Object { W ("  " + $_.Line) }
} else {
    W 'no trace log (core may not have loaded)'
}

# Independent check: which graphics DLLs does the process actually have?
W '--- loaded graphics modules (via PEB walk not available from here) ---'
W 'rely on the in-process probe output above'

W '=== d3d11 mode test done ==='
