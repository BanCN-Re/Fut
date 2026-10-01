# monitor_live.ps1 -- continuous health monitor for the running game + cheat core.
# Watches: game process, trace-log frame advance, entity/draw metrics, crash signals.
# Exit codes (the parent agent restarts this on every exit, forming an unbroken chain):
#   0  = window elapsed, all healthy         10 = game process gone
#   11 = trace error keyword                 12 = frame stall (>45s no advance)
#   13 = game came up after being down       (crash classifier in output)
$ErrorActionPreference = 'Continue'
$Game   = 'Calabiyau-Win64-Shipping'
$Trace  = 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64\fut_cheat.trace.log'
$Out    = 'D:\Work\Fut\evidence\live_monitor.log'
$CrashDirs = @(
  'D:\CLBQ\CalabiYau\CalabiyauGame\Saved\Crashes',
  "$env:LOCALAPPDATA\Calabiyau\Saved\Crashes"
)
$WindowMin = 8

function W([string]$m) {
  $line = "$(Get-Date -Format 'HH:mm:ss') $m"
  $line | Out-File $Out -Append -Encoding ASCII
  Write-Host $line
}

"=== monitor start $(Get-Date -Format 'HH:mm:ss') ===" | Out-File $Out -Encoding ASCII
$sw = [Diagnostics.Stopwatch]::StartNew()
$t0 = Get-Date
$lastLen = 0; $lastFrame = 0; $lastFrameAt = Get-Date
$lastBeat = Get-Date; $everSawGame = $false
$bestTgt = 0; $lastDrawn = -1; $lastProj = ''

while ($sw.Elapsed.TotalMinutes -lt $WindowMin) {
  Start-Sleep -Seconds 2
  $g = Get-Process -Name $Game -ErrorAction SilentlyContinue | Select-Object -First 1

  if (-not $g) {
    if ($everSawGame) {
      W "GAME GONE after $([int]$sw.Elapsed.TotalSeconds)s (was pid=$($gpid))"
      $cr = Get-Process -Name 'CrashReportClient*','UE4Editor-CrashReportClient*' -ErrorAction SilentlyContinue
      if ($cr) { W "VERDICT: CRASH -- CrashReportClient pid=$($cr.Id -join ',')" }
      else {
        $dmp = @()
        foreach ($d in $CrashDirs) {
          if (Test-Path $d) { $dmp += Get-ChildItem $d -Recurse -Filter '*.dmp' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $t0 } }
        }
        if ($dmp) { W "VERDICT: CRASH -- new dump: $($dmp[0].FullName)" } else { W "VERDICT: clean exit (no crash handler, no new dump)" }
      }
      W "=== monitor end (exit 10) ==="
      exit 10
    }
    # not up yet at monitor start; keep waiting inside the window
  } else {
    $everSawGame = $true; $script:gpid = $g.Id
  }

  if ($g -and (Test-Path $Trace)) {
    $len = (Get-Item $Trace).Length
    if ($len -lt $lastLen) { $lastLen = 0 }                # log was cleared/redeployed
    if ($len -gt $lastLen) {
      $fs = [IO.File]::Open($Trace, 'Open', 'Read', 'ReadWrite')
      [void]$fs.Seek($lastLen, 'Begin')
      $sr = New-Object IO.StreamReader($fs)
      $new = $sr.ReadToEnd(); $sr.Close(); $fs.Close()
      $lastLen = $len
      foreach ($ln in ($new -split "`r?`n")) {
        if ($ln -match 'Fatal|EXCEPTION|LowLevelFatal|assert|abort|\[err\]') { W "TRACE-ERROR: $ln"; W '=== monitor end (exit 11) ==='; exit 11 }
        if ($ln -match 'frame (\d+)\s+menu=(\w+)\s+obj=(\d+)\s+tgt=(\d+)\s+path=(\w+)') {
          $f = [int]$Matches[1]
          if ($f -gt $lastFrame) { $lastFrame = $f; $lastFrameAt = Get-Date }
          if ([int]$Matches[4] -gt $bestTgt) { $bestTgt = [int]$Matches[4] }
        }
        if ($ln -match 'stage=DRAW ents=(\d+) drawn=(\d+) proj=(\d+).*ok=(\d+) bad=(\d+)') {
          $lastDrawn = [int]$Matches[2]; $lastProj = "ok=$($Matches[4]) bad=$($Matches[5])"
        }
      }
    }
    # frame stall: overlay stopped advancing while game still runs
    if ($lastFrame -gt 0 -and ((Get-Date) - $lastFrameAt).TotalSeconds -gt 45) {
      W "FRAME STALL: frame=$lastFrame frozen >45s, game pid=$($g.Id) alive"
      W '=== monitor end (exit 12) ==='
      exit 12
    }
  }

  # 60s heartbeat: pid / cpu / mem / frame / tgt / drawn
  if (((Get-Date) - $lastBeat).TotalSeconds -ge 60) {
    $lastBeat = Get-Date
    if ($g) {
      $cpu = '{0:N0}' -f $g.CPU; $ws = '{0:N0}' -f ($g.WorkingSet64 / 1MB)
      W "beat pid=$($g.Id) cpu=${cpu}s ws=${ws}MB frame=$lastFrame tgt=$bestTgt drawn=$lastDrawn $lastProj"
    } else {
      W "beat game=down (waiting) frame=$lastFrame"
    }
  }
}
W "window ok: frame=$lastFrame tgt=$bestTgt drawn=$lastDrawn $lastProj"
W '=== monitor end (exit 0 -- restarting) ==='
exit 0
