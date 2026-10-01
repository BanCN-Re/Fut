# watch_and_deploy.ps1 -- waits for the game to exit NATURALLY (never kills it),
# then triggers the standard deploy + relaunch and reports boot status.
$ErrorActionPreference = 'Continue'
$work = 'D:\xingyun\tools\work\clbq_work.txt'
$flag = 'D:\xingyun\tools\work\clbq_work_done.flag'

"=== watch start $(Get-Date -Format 'HH:mm:ss') -- waiting for natural game exit ==="

# Phase 1: wait for the game to close (poll 5 s, up to 6 h)
$deadline = (Get-Date).AddHours(6)
while ((Get-Date) -lt $deadline) {
    $p = Get-Process 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
    if (-not $p) { break }
    Start-Sleep -Seconds 5
}
$p = Get-Process 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
if ($p) { "timeout: game still running after 6h; aborting auto-deploy"; exit 20 }

"game exited at $(Get-Date -Format 'HH:mm:ss') -- deploying in 15 s unless it restarts itself"
Start-Sleep -Seconds 15
$p = Get-Process 'Calabiyau-Win64-Shipping' -ErrorAction SilentlyContinue
if ($p) { "game relaunched by user; standing down (no deploy while it runs)"; exit 21 }

# Phase 2: deploy via the elevated runner
Set-Content $work 'D:\Work\Fut\tools\work\deploy_core.ps1' -NoNewline
Remove-Item $flag -Force -ErrorAction SilentlyContinue
Remove-Item 'D:\xingyun\tools\work\clbq_runner.lock' -Force -ErrorAction SilentlyContinue
Start-ScheduledTask -TaskName 'ClbqAdmin'
"deploy queued at $(Get-Date -Format 'HH:mm:ss')"

# Phase 3: wait for the deploy script's done flag (up to 7 min)
for ($i = 0; $i -lt 84; $i++) {
    Start-Sleep -Seconds 5
    if (Test-Path $flag) { break }
}
"--- deploy_core.log tail ---"
Get-Content 'D:\Work\Fut\evidence\deploy_core.log' -ErrorAction SilentlyContinue | Select-Object -Last 10

# Phase 4: boot health -- 30 s of trace after Shipping comes up
Start-Sleep -Seconds 30
$log = 'D:\CLBQ\CalabiYau\CalabiYauGame\PM\Binaries\Win64\fut_cheat.trace.log'
"--- trace key lines ---"
if (Test-Path $log) {
    Get-Content $log | Select-String -Pattern 'boot\] ready|font|refskeleton|hp probe|idle|error|EXCEPTION' |
        Select-Object -Last 15 | ForEach-Object { $_.Line }
}
"=== watch end ==="
