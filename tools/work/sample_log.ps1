# sample_log.ps1 -- DeepSeek-style one-shot trace sampler.
# Sleeps $SleepS seconds, then pulls the key health lines from fut_cheat.trace.log,
# checks the game process, and exits. The parent agent chains these: on every
# completion notification it reads the output and starts the next one.
param([int]$SleepS = 150)
$ErrorActionPreference = 'Continue'
$Trace = 'D:\CLBQ\CalabiYau\CalabiYauGame\PM\Binaries\Win64\fut_cheat.trace.log'
Start-Sleep -Seconds $SleepS

"=== sample @ $(Get-Date -Format 'HH:mm:ss') (after ${SleepS}s) ==="
$p = Get-Process 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
if ($p) {
  "GAME: alive pid=$($p.Id) up=$([int]((Get-Date)-$p.StartTime).TotalSeconds)s cpu=$([int]$p.CPU)s ws=$([int]($p.WorkingSet64/1MB))MB"
} else {
  "GAME: NOT RUNNING"
}

if (Test-Path $Trace) {
  $lines = Get-Content $Trace
  "TRACE: $($lines.Count) lines"
  "-- errors --"
  $lines | Select-String -Pattern 'Fatal|EXCEPTION|LowLevel|\[err\]|assert' | Select-Object -Last 4 | ForEach-Object { $_.Line }
  "-- skeleton rig (joint match) --"
  $lines | Select-String -Pattern '\[skel\] rig' | Select-Object -Last 4 | ForEach-Object { $_.Line }
  "-- bone probe (head/feet Z) --"
  $lines | Select-String -Pattern 'bone probe' | Select-Object -Last 3 | ForEach-Object { $_.Line }
  "-- draw stage --"
  $lines | Select-String -Pattern 'stage=DRAW' | Select-Object -Last 3 | ForEach-Object { $_.Line }
  "-- overlay frames --"
  $lines | Select-String -Pattern '\[overlay\] frame' | Select-Object -Last 3 | ForEach-Object { $_.Line }
  "-- boot / font / input --"
  $lines | Select-String -Pattern '\[boot\] ready|font.*loaded|click-through|RegisterHotKey OK' | Select-Object -Last 4 | ForEach-Object { $_.Line }
} else {
  "TRACE: missing"
}
"=== sample end ==="
